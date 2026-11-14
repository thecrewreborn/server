#include "core/v1_00/cars.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

#include "core/v1_21/gamedata.h"
#include "core/v1_21/pack.h"
#include "core/wire.h"

namespace tcr {
namespace v1_00 {
namespace cars {

namespace {

namespace cars121 = v1_21::cars;
namespace pack = v1_21::pack;

struct Data {
  int64_t parts = 0;
  int64_t perf_parts = 0;
  std::unordered_map<uint64_t, std::vector<int64_t>> stock;
};

const Data& data() {
  static const Data d = [] {
    Data out;
    const json::Value* t = v1_21::gamedata::table("v1_00_cars.json");
    if (!t || !t->is_object()) return out;
    if (const json::Value* v = t->get("parts")) out.parts = v->as_i64().value_or(0);
    if (const json::Value* v = t->get("perf_parts")) out.perf_parts = v->as_i64().value_or(0);
    if (const json::Value* slots = t->get("slots")) {
      for (const json::Member& m : slots->members()) {
        std::vector<int64_t> s;
        for (const json::Value& x : m.value.items()) s.push_back(x.as_i64().value_or(0));
        out.stock.emplace(v1_21::gamedata::hex_id(m.key), std::move(s));
      }
    }
    return out;
  }();
  return d;
}

const std::vector<size_t>& list_slots() {
  static const std::vector<size_t> s = [] {
    std::vector<size_t> out;
    for (size_t i : cars121::list_slots()) {
      if (i >= KEPT_FIRST && i < KEPT_END) out.push_back(i);
    }
    return out;
  }();
  return s;
}

uint64_t u64(const Bytes& b, size_t o) { return wire::get_u64(b.data() + o); }

bool slots_and_variant(const Bytes& cfg, std::vector<int64_t>* s121, uint64_t* variant, Raised* raised) {
  if (!pack::unpack_check(cfg, 35, 2 * cars121::SLOT_COUNT, raised)) return false;
  s121->clear();
  for (size_t i = 0; i < cars121::SLOT_COUNT; i++) {
    s121->push_back(static_cast<int64_t>(cfg[35 + 2 * i]) << 8 | cfg[36 + 2 * i]);
  }
  if (!pack::unpack_check(cfg, 8, 8, raised)) return false;
  *variant = u64(cfg, 8);
  return true;
}

bool owned_from_121(const Bytes& cfg, std::vector<uint64_t>* words, Raised* raised) {
  size_t o = 35 + 76;
  uint32_t first = 0;
  if (!pack::unpack_u32(cfg, o, &first, raised)) return false;
  o += 4 + first;
  uint32_t n = 0;
  if (!pack::unpack_u32(cfg, o + 16, &n, raised)) return false;
  if (!pack::unpack_check(cfg, o + 20, 8 * static_cast<size_t>(n), raised)) return false;
  words->assign(OWNED_WORDS, 0);
  for (size_t k = 0; k < std::min<size_t>(n, OWNED_WORDS); k++) (*words)[k] = u64(cfg, o + 20 + 8 * k);
  int64_t perf = perf_parts();
  for (size_t k = 0; k < OWNED_WORDS; k++) {
    int64_t lo = 64 * static_cast<int64_t>(k);
    if (lo + 64 > perf) {
      int64_t keep = std::max<int64_t>(0, perf - lo);
      (*words)[k] &= keep >= 64 ? ~uint64_t(0) : (uint64_t(1) << keep) - 1;
    }
  }
  return true;
}

void put_u16s(Bytes& out, const std::vector<int64_t>& v) {
  for (int64_t x : v) wire::put_u16(out, static_cast<uint16_t>(x));
}

}

Bytes no_car() {
  Bytes out;
  wire::put_u64(out, 0xFFFFFFFFFFFFFFFFull);
  Bytes e = wire::empty_array();
  out.insert(out.end(), e.begin(), e.end());
  out.insert(out.end(), e.begin(), e.end());
  return out;
}

int64_t parts() { return data().parts; }
int64_t perf_parts() { return data().perf_parts; }

std::vector<int64_t> slots_from_121(const std::vector<int64_t>& s121, uint64_t variant,
                                    const cars121::Catalog* catalog) {
  std::vector<int64_t> kept(s121.begin() + std::min<size_t>(11, s121.size()),
                            s121.begin() + std::min<size_t>(25, s121.size()));
  auto stock = data().stock.find(variant);
  if (stock != data().stock.end()) {
    std::vector<std::optional<int64_t>> stock121;
    if (catalog) {
      if (const cars121::Variant* v = catalog->variant(variant)) {
        for (size_t i = 11; i < std::min<size_t>(25, v->slots.size()); i++) stock121.push_back(v->slots[i]);
      }
    }
    for (int i = 0; i < 14; i++) stock121.push_back(std::nullopt);
    size_t n = std::min({kept.size(), stock121.size(), stock->second.size()});
    std::vector<int64_t> mapped;
    for (size_t k = 0; k < n; k++) {
      mapped.push_back(stock121[k] && kept[k] == *stock121[k] ? stock->second[k] : kept[k]);
    }
    kept = std::move(mapped);
  }
  std::vector<int64_t> out;
  for (size_t i = 0; i < std::min<size_t>(11, s121.size()); i++) {
    out.push_back(s121[i] < perf_parts() ? s121[i] : EMPTY);
  }
  out.resize(out.size() + (SLOTS - 11), EMPTY);
  for (size_t k = 0; k < kept.size() && 11 + k < out.size(); k++) out[11 + k] = kept[k] < parts() ? kept[k] : EMPTY;
  return out;
}

bool config_from_121(const Bytes& cfg, const cars121::Catalog* catalog, Bytes* out, Raised* raised) {
  Bytes head(cfg.begin(), cfg.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(35, cfg.size())));
  if (!pack::unpack_check(cfg, 8, 8, raised)) return false;
  uint64_t variant = u64(cfg, 8);
  if (!pack::unpack_check(cfg, 35, 2 * cars121::SLOT_COUNT, raised)) return false;
  std::vector<int64_t> s121;
  for (size_t i = 0; i < cars121::SLOT_COUNT; i++) {
    s121.push_back(static_cast<int64_t>(cfg[35 + 2 * i]) << 8 | cfg[36 + 2 * i]);
  }
  Bytes slots;
  put_u16s(slots, slots_from_121(s121, variant, catalog));
  Bytes ids;
  for (size_t i = 0; i < SLOTS; i++) wire::put_u32(ids, 0xFFFFFFFF);
  std::vector<uint64_t> owned;
  if (!owned_from_121(cfg, &owned, raised)) return false;
  Bytes words;
  for (uint64_t w : owned) wire::put_u64(words, w);
  *out = head;
  out->insert(out->end(), slots.begin(), slots.end());
  Bytes a = cars121::array(SLOTS, ids);
  out->insert(out->end(), a.begin(), a.end());
  Bytes b = cars121::array(OWNED_WORDS, words);
  out->insert(out->end(), b.begin(), b.end());
  return true;
}

Bytes part_list(const std::vector<int64_t>& s) {
  std::vector<int64_t> shown;
  for (size_t i : list_slots()) {
    if (i < s.size() && s[i] != EMPTY) shown.push_back(s[i]);
  }
  Bytes items;
  put_u16s(items, shown);
  return cars121::array(static_cast<uint32_t>(shown.size()), items);
}

bool garage_field(const cars121::Garage& entries, const cars121::Catalog* catalog, Bytes* out, Raised* raised) {
  cars121::Garage converted;
  for (const auto& e : entries) {
    std::vector<Bytes> cs;
    for (const Bytes& c : e.second) {
      Bytes cfg;
      if (!config_from_121(c, catalog, &cfg, raised)) return false;
      cs.push_back(std::move(cfg));
    }
    converted.emplace_back(e.first, std::move(cs));
  }
  std::optional<Bytes> field = cars121::garage_field(converted, raised);
  if (!field) return false;
  *out = std::move(*field);
  return true;
}

bool parts_field(const cars121::Garage& entries, const json::Value& owned, const cars121::Catalog* catalog,
                 Bytes* out, Raised* raised) {
  obj::Py py;
  std::vector<json::Value> words(PLAYER_PARTS, obj::I(0));
  std::vector<json::Value> given = py.iter(owned);
  if (py.failed()) {
    *raised = py.raised();
    return false;
  }
  for (size_t k = 0; k < std::min(given.size(), PLAYER_PARTS); k++) words[k] = given[k];
  for (const auto& e : entries) {
    for (const Bytes& cfg : e.second) {
      std::vector<int64_t> s121;
      uint64_t variant = 0;
      if (!slots_and_variant(cfg, &s121, &variant, raised)) return false;
      std::vector<int64_t> s = slots_from_121(s121, variant, catalog);
      for (size_t i = KEPT_FIRST; i < KEPT_END && i < s.size(); i++) {
        int64_t x = s[i];
        if (x == EMPTY) continue;
        json::Value& w = words[static_cast<size_t>(x >> 6)];
        w = py.bor(w, obj::U(uint64_t(1) << (x & 63)), true);
        if (py.failed()) {
          *raised = py.raised();
          return false;
        }
      }
    }
  }
  int64_t all = parts();
  for (size_t k = 0; k < PLAYER_PARTS; k++) {
    int64_t lo = 64 * static_cast<int64_t>(k);
    if (lo + 64 > all) {
      int64_t keep = std::max<int64_t>(0, all - lo);
      json::Value mask = obj::U(keep >= 64 ? ~uint64_t(0) : (uint64_t(1) << keep) - 1);
      words[k] = py.band(words[k], mask, true);
      if (py.failed()) {
        *raised = py.raised();
        return false;
      }
    }
  }
  bool any = false;
  for (const json::Value& w : words) any = any || obj::truthy(w);
  if (!any) {
    *out = wire::empty_array();
    return true;
  }
  Bytes items;
  for (const json::Value& w : words) {
    if (!py.pack(items, 'Q', w)) {
      *raised = py.raised();
      return false;
    }
  }
  *out = cars121::array(PLAYER_PARTS, items);
  return true;
}

bool car_reply_from_121(const Bytes& car, const cars121::Catalog* catalog, Bytes* out, Raised* raised) {
  if (!pack::unpack_check(car, 0, 8, raised)) return false;
  uint64_t model = u64(car, 0);
  uint32_t n = 0;
  if (!pack::unpack_u32(car, 8, &n, raised)) return false;
  size_t start = std::min<size_t>(12, car.size());
  Bytes frame(car.begin() + static_cast<std::ptrdiff_t>(start),
              car.begin() + static_cast<std::ptrdiff_t>(std::min(car.size(), 12 + static_cast<size_t>(n))));
  uint32_t count = 0;
  if (!pack::unpack_u32(frame, 12, &count, raised)) return false;
  size_t size = count ? (frame.size() - 16) / count : 0;
  auto config = [&](size_t k) {
    size_t a = std::min(frame.size(), 16 + k * size), b = std::min(frame.size(), 16 + (k + 1) * size);
    return Bytes(frame.begin() + static_cast<std::ptrdiff_t>(a), frame.begin() + static_cast<std::ptrdiff_t>(b));
  };
  if (model == 0xFFFFFFFFFFFFFFFFull || count == 0) {
    *out = no_car();
    return true;
  }
  Bytes joined;
  for (size_t k = 0; k < count; k++) {
    Bytes cfg;
    if (!config_from_121(config(k), catalog, &cfg, raised)) return false;
    joined.insert(joined.end(), cfg.begin(), cfg.end());
  }
  Bytes last = config(count - 1);
  std::vector<int64_t> s121;
  uint64_t variant = 0;
  if (!slots_and_variant(last, &s121, &variant, raised)) return false;
  *out = Bytes();
  wire::put_u64(*out, model);
  Bytes a = cars121::array(count, joined);
  out->insert(out->end(), a.begin(), a.end());
  Bytes list = part_list(slots_from_121(s121, variant, catalog));
  out->insert(out->end(), list.begin(), list.end());
  return true;
}

}
}
}
