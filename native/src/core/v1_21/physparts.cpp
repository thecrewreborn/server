#include "core/v1_21/physparts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "core/core.h"
#include "core/json.h"
#include "core/v1_21/cars.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/rewards.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {
namespace physparts {

const char REWARD_PART[] = "CAbility_RewardPart";

namespace {

using gamedata::int_of;

const Table* embedded() {
  static const std::unique_ptr<Table> t = []() -> std::unique_ptr<Table> {
    const json::Value* d = gamedata::table("physparts.json");
    if (!d || !d->is_object()) return nullptr;
    std::unique_ptr<Table> out(new Table());
    if (const json::Value* ids = d->get("ids")) {
      for (const json::Value& x : ids->items()) out->ids.push_back(x.is_string() ? gamedata::hex_id(x.as_string()) : 0);
    }
    if (const json::Value* parts = d->get("parts")) {
      for (const json::Value& x : parts->items()) {
        std::array<int64_t, 4> e{};
        for (size_t i = 0; i < 4 && i < x.items().size(); i++) e[i] = int_of(&x.items()[i]);
        out->parts.push_back(e);
      }
    }
    if (const json::Value* missions = d->get("missions")) {
      for (const json::Member& m : missions->members()) {
        Mission mi;
        mi.slot = int_of(m.value.get("slot"));
        mi.level = int_of(m.value.get("level"));
        if (const json::Value* q = m.value.get("quality")) {
          for (const json::Value& x : q->items()) mi.quality.push_back(int_of(&x));
        }
        if (const json::Value* b = m.value.get("bonus")) {
          for (const json::Value& x : b->items()) {
            mi.bonus.push_back(x.is_float() ? x.as_float() : static_cast<double>(int_of(&x)));
          }
        }
        out->missions[m.key] = std::move(mi);
      }
    }
    return out;
  }();
  return t.get();
}

bool owned_at(const Bytes& cfg, size_t* o, Raised* raised) {
  uint32_t first = 0;
  if (!pack::unpack_u32(cfg, 0x6F, &first, raised)) return false;
  *o = 0x6F + 4 + static_cast<size_t>(first);
  return true;
}

}

const Table* table() { return embedded(); }

std::optional<int64_t> find(const Table& t, int64_t slot, int64_t quality, int64_t level, int64_t bonus) {
  std::optional<int64_t> best;
  for (size_t i = 0; i < t.parts.size(); i++) {
    const auto& e = t.parts[i];
    int64_t s = e[0], q = e[1], lv = e[2], b = e[3];
    if (s == slot && q == quality && (b == bonus || quality == BRONZE || quality == CAU_PLATINUM) && lv <= level) {
      if (!best || lv > t.parts[static_cast<size_t>(*best)][2]) best = static_cast<int64_t>(i);
    }
  }
  return best;
}

bool owned(const Bytes& cfg, std::vector<int64_t>* out, Raised* raised) {
  size_t o = 0;
  if (!owned_at(cfg, &o, raised)) return false;
  uint32_t n = 0;
  if (!pack::unpack_u32(cfg, o + 16, &n, raised)) return false;
  if (!pack::unpack_check(cfg, o + 20, static_cast<size_t>(n) * 8, raised)) return false;
  out->clear();
  for (size_t w = 0; w < n; w++) {
    uint64_t x = wire::get_u64(cfg.data() + o + 20 + 8 * w);
    for (int b = 0; b < 64; b++) {
      if (x >> b & 1) out->push_back(static_cast<int64_t>(64 * w + static_cast<size_t>(b)));
    }
  }
  return true;
}

std::optional<Bytes> own(const Bytes& cfg, int64_t part, Raised* raised) {
  size_t o = 0;
  if (!owned_at(cfg, &o, raised)) return std::nullopt;
  uint32_t size = 0, n = 0;
  if (!pack::unpack_u32(cfg, o, &size, raised)) return std::nullopt;
  if (!pack::unpack_u32(cfg, o + 16, &n, raised)) return std::nullopt;
  if (part >= 0 && (part >> 6) >= static_cast<int64_t>(n)) return cfg;
  if (!pack::unpack_check(cfg, o + 20, static_cast<size_t>(n) * 8, raised)) return std::nullopt;
  std::vector<uint64_t> words(n);
  for (size_t w = 0; w < n; w++) words[w] = wire::get_u64(cfg.data() + o + 20 + 8 * w);
  int64_t idx = part >= 0 ? (part >> 6) : static_cast<int64_t>(n) + ((part + 1) / 64 - 1);
  if (idx < 0 || idx >= static_cast<int64_t>(n)) {
    *raised = Raised{"IndexError", "list index out of range"};
    return std::nullopt;
  }
  words[static_cast<size_t>(idx)] |= uint64_t(1) << (static_cast<uint64_t>(part) & 63);
  Bytes items;
  for (uint64_t w : words) wire::put_u64(items, w);
  Bytes out(cfg.begin(), cfg.begin() + static_cast<std::ptrdiff_t>(o));
  Bytes arr = cars::array(n, items);
  out.insert(out.end(), arr.begin(), arr.end());
  size_t tail = o + 4 + static_cast<size_t>(size);
  if (tail < cfg.size()) out.insert(out.end(), cfg.begin() + static_cast<std::ptrdiff_t>(tail), cfg.end());
  return out;
}

std::optional<Bytes> fit(const Bytes& cfg, int64_t part, int64_t slot, Raised* raised) {
  Bytes out = cfg;
  int64_t offset = 0x23 + 2 * slot;
  if (offset < 0 || static_cast<size_t>(offset) + 2 > out.size()) {
    *raised = pack::error("pack_into requires a buffer of at least " + py::int_to_text(offset + 2) +
                          " bytes for packing 2 bytes at offset " + py::int_to_text(offset) +
                          " (actual buffer size is " + py::uint_to_text(out.size()) + ")");
    return std::nullopt;
  }
  Bytes h;
  if (!pack::put(h, 'H', pack::Num::of_signed(part), raised)) return std::nullopt;
  out[static_cast<size_t>(offset)] = h[0];
  out[static_cast<size_t>(offset) + 1] = h[1];
  return out;
}

bool give(Player& p, uint64_t model, uint64_t spec, const std::vector<int64_t>& parts, bool mount, bool* given,
          Raised* raised) {
  std::optional<cars::Garage> garage = cars::entries(p, raised);
  if (!garage) return false;
  std::vector<Bytes>* configs = cars::configs_of(*garage, obj::U(model));
  *given = false;
  if (!configs) return true;
  size_t k = configs->size();
  for (size_t i = 0; i < configs->size(); i++) {
    std::optional<uint64_t> s = cars::spec((*configs)[i], raised);
    if (!s) return false;
    if (*s == spec) {
      k = i;
      break;
    }
  }
  if (k == configs->size()) return true;
  Bytes cfg = (*configs)[k];
  const Table* t = table();
  for (int64_t part : parts) {
    std::optional<Bytes> next = own(cfg, part, raised);
    if (!next) return false;
    cfg = std::move(*next);
    if (mount && t) {
      if (part < -static_cast<int64_t>(t->parts.size()) || part >= static_cast<int64_t>(t->parts.size())) {
        *raised = Raised{"IndexError", "list index out of range"};
        return false;
      }
      size_t at = static_cast<size_t>(part < 0 ? part + static_cast<int64_t>(t->parts.size()) : part);
      std::optional<Bytes> fitted = fit(cfg, part, t->parts[at][0], raised);
      if (!fitted) return false;
      cfg = std::move(*fitted);
    }
  }
  (*configs)[k] = std::move(cfg);
  cars::store(p, *garage);
  *given = true;
  return true;
}

bool has_ability(obj::Py& py, const Player& p, const std::string& name, const Core& core) {
  const rewards::Tables* t = rewards::tables(core);
  if (!t) return false;
  auto it = t->abilities.find(name);
  if (it == t->abilities.end()) return false;
  int64_t bit = it->second;
  size_t len = py.len(p.abilities);
  if (py.failed() || static_cast<size_t>(bit / 64) >= len) return false;
  V set = py.band(py.rshift(py.getitem(p.abilities, obj::I(bit / 64)), obj::I(bit % 64)), obj::I(1));
  return !py.failed() && obj::eq(set, obj::I(1));
}

bool mission_part(Player& p, uint64_t mission, int64_t medal, uint64_t model, uint64_t spec, Random& rng,
                  const Core& core, std::optional<int64_t>* part, Raised* raised) {
  part->reset();
  const Table* t = table();
  const Mission* m = nullptr;
  if (t) {
    auto it = t->missions.find(gamedata::hex_key(mission));
    if (it != t->missions.end()) m = &it->second;
  }
  if (!REWARD_PARTS || !m || !(0 <= medal && medal < 4)) return true;
  obj::Py py;
  bool able = has_ability(py, p, REWARD_PART, core);
  if (py.failed()) {
    *raised = py.raised();
    return false;
  }
  if (!able) return true;
  size_t qi = static_cast<size_t>(std::min<int64_t>(medal, 2));
  if (qi >= m->quality.size()) {
    *raised = Raised{"IndexError", "list index out of range"};
    return false;
  }
  int64_t quality = m->quality[qi];
  if (quality < 0) return true;
  int64_t bonus = NO_BONUS;
  if (quality != BRONZE && quality != CAU_PLATINUM) {
    if (m->bonus.size() != 4) {
      *raised = Raised{"ValueError", "The number of weights does not match the population"};
      return false;
    }
    double total = ((m->bonus[0] + m->bonus[1]) + m->bonus[2]) + m->bonus[3] + 0.0;
    if (total <= 0.0) {
      *raised = Raised{"ValueError", "Total of weights must be greater than zero"};
      return false;
    }
    if (!std::isfinite(total)) {
      *raised = Raised{"ValueError", "Total of weights must be finite"};
      return false;
    }
    std::optional<size_t> i = rng.choices_index(m->bonus.data(), m->bonus.size());
    bonus = i ? static_cast<int64_t>(*i) : 0;
  }
  std::optional<int64_t> found = find(*t, m->slot, quality, m->level, bonus);
  if (!found) return true;
  bool given = false;
  if (!give(p, model, spec, {*found}, false, &given, raised)) return false;
  if (given) *part = found;
  return true;
}

}
}
}
