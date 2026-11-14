#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <unordered_map>

#include "core/config.h"
#include "core/core.h"
#include "core/ubi.h"
#include "core/v1_21/cars.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/peers.h"
#include "core/v1_21/summit.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

constexpr size_t CONDITION = 0x11;
constexpr size_t MAX_MODEL_CONFIGS = 254;
constexpr size_t MAX_GARAGE_CONFIGS = 1100;
constexpr size_t CAR_DISCOUNT_PERK = 14;
constexpr int64_t CAR_DISCOUNT_STEP = 3;
constexpr int64_t CAR_DISCOUNT_LEVELS = 10;

Raised bytes_index_error() { return Raised{"IndexError", "index out of range"}; }

Bytes slice(const Bytes& b, size_t start, size_t stop) {
  size_t a = std::min(start, b.size()), e = std::min(stop, b.size());
  return Bytes(b.begin() + static_cast<std::ptrdiff_t>(a), b.begin() + static_cast<std::ptrdiff_t>(std::max(a, e)));
}

void append(Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); }

int64_t cc_price(int64_t price) { return static_cast<int64_t>(static_cast<double>(price) / cars::CC_RATE); }

using obj::I;
using obj::Py;
using obj::U;

std::vector<Bytes>* configs_of(cars::Garage& garage, uint64_t model) { return cars::configs_of(garage, U(model)); }

bool model_set(Py& py, const cars::Garage& garage, std::vector<V>* models) {
  for (const auto& e : garage) {
    if (!obj::hashable(e.first)) {
      return py.raise("TypeError", std::string("unhashable type: '") + obj::type_name(e.first) + "'");
    }
    models->push_back(e.first);
  }
  return true;
}

bool has_model(const std::vector<V>& models, uint64_t m) {
  for (const V& x : models) {
    if (obj::same(x, U(m))) return true;
  }
  return false;
}

bool garage_full(const cars::Garage& garage, const std::vector<Bytes>* configs) {
  if (configs && configs->size() >= MAX_MODEL_CONFIGS) return true;
  size_t total = 0;
  for (const auto& e : garage) total += e.second.size();
  return total >= MAX_GARAGE_CONFIGS;
}

bool dlc_only(const cars::Catalog& c, const cars::Variant& v, const Player& p) {
  return p.dlc.empty() && c.special(v.spec);
}

V car_price(Py& py, int64_t price, const V& perk_level) {
  V level = py.min(py.max(perk_level, I(0)), I(CAR_DISCOUNT_LEVELS));
  V x = py.mul(I(price), py.sub(I(100), py.mul(I(CAR_DISCOUNT_STEP), level)));
  return py.max(I(0), py.floordiv(x, I(100)));
}

HandlerResult car_report(Ctx& ctx, const Bytes& body, const Registered&) {
  Player& p = ctx.player;
  Raised raised;
  if (body.size() >= 0x59) {
    bool repair = body.back() == 1 || body.back() == 4;
    size_t o = repair ? 0x1C : 0x48;
    uint64_t model = wire::get_u64(body.data() + o), spec = wire::get_u64(body.data() + o + 8);
    uint8_t value = repair ? 100 : body[0x58];
    std::optional<cars::Garage> garage = cars::entries(p, &raised);
    if (!garage) return HandlerResult::fail(raised);
    for (auto& e : *garage) {
      std::vector<Bytes>& cs = e.second;
      for (size_t k = 0; k < cs.size(); k++) {
        if (!obj::eq(e.first, U(model))) continue;
        std::optional<uint64_t> s = cars::spec(cs[k], &raised);
        if (!s) return HandlerResult::fail(raised);
        if (*s != spec) continue;
        if (cs[k].size() <= CONDITION) return HandlerResult::fail(bytes_index_error());
        if (cs[k][CONDITION] == value) continue;
        cs[k][CONDITION] = value;
        cars::store(p, *garage);
        if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
        break;
      }
    }
  }
  pack::Packer pk;
  pk.B(1).I(0).I(p.bucks).H(0);
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  return HandlerResult::reply(0x67ED13A9, std::move(pk.out()));
}

HandlerResult peer_register(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised raised;
  bool on = false;
  if (!peers::enabled(*ctx.core, &on, &raised)) return HandlerResult::fail(raised);
  if (on) {
    std::optional<uint64_t> pid;
    if (!peers::register_peer(*ctx.core, body, &ctx, &pid, &raised)) return HandlerResult::fail(raised);
  }
  Bytes out{1};
  wire::put_u32(out, peers::SESSION_NUMBER);
  return HandlerResult::reply(0x56CEFAEE, std::move(out));
}

std::optional<size_t> token_of(uint8_t currency, uint64_t spec) {
  if (currency == 8 && spec == 0x7938316) return 0;
  if (currency == 0x10 && spec == 0x7938315) return 1;
  if (currency == 0x20 && spec == 0x7938317) return 2;
  return std::nullopt;
}

struct SpecResult {
  Bytes element;
  bool changed = false;
};

std::optional<SpecResult> spec_element(Ctx& ctx, const Bytes& body, Raised* raised) {
  Player& p = ctx.player;
  const cars::Catalog* c = cars::catalog(*ctx.core);
  if (body.size() <= 0x1C) {
    *raised = bytes_index_error();
    return std::nullopt;
  }
  uint8_t currency = body[0x1C];
  uint32_t count = 0;
  if (!pack::unpack_u32(body, 0x1D + 16, &count, raised)) return std::nullopt;
  std::vector<int64_t> zeros(cars::SLOT_COUNT, 0);
  SpecResult r;
  if (count == 0) {
    wire::put_u64(r.element, cars::NONE);
    append(r.element, cars::array(1, cars::empty_config()));
    std::optional<Bytes> listed = cars::part_list(zeros, raised);
    if (!listed) return std::nullopt;
    append(r.element, *listed);
    return r;
  }
  if (!pack::unpack_check(body, 0x1D + 20, 8, raised)) return std::nullopt;
  uint64_t model = wire::get_u64(body.data() + 0x1D + 20);
  SpecResult fail;
  wire::put_u64(fail.element, model);
  append(fail.element, cars::array(1, cars::empty_config()));
  std::optional<Bytes> listed = cars::part_list(zeros, raised);
  if (!listed) return std::nullopt;
  append(fail.element, *listed);
  uint32_t n = 0;
  if (!pack::unpack_u32(body, 0x1D + 28 + 16, &n, raised)) return std::nullopt;
  if (!c || n == 0) return fail;
  size_t at = 0x1D + 28 + 20;
  if (!pack::unpack_check(body, at, 80, raised)) return std::nullopt;
  uint64_t variant = wire::get_u64(body.data() + at);
  uint64_t parts[9];
  for (size_t i = 0; i < 9; i++) parts[i] = wire::get_u64(body.data() + at + 8 + 8 * i);
  std::optional<cars::Garage> garage = cars::entries(p, raised);
  if (!garage) return std::nullopt;
  std::vector<Bytes>* configs = configs_of(*garage, model);
  const cars::Variant* v = c->variant(variant);
  if (!configs || configs->empty() || !v) return fail;
  if (dlc_only(*c, *v, p)) return fail;
  std::optional<int64_t> price;
  if (!cars::spec_price(*c, *garage, *v, &price, raised)) return std::nullopt;
  std::optional<size_t> owned;
  for (size_t i = 0; i < configs->size(); i++) {
    std::optional<uint64_t> vo = cars::variant_of((*configs)[i], raised);
    if (!vo) return std::nullopt;
    if (*vo == variant) {
      owned = i;
      break;
    }
  }
  if (owned && !price) return fail;
  if (!owned && garage_full(*garage, configs)) return fail;
  if (price) {
    std::optional<size_t> token = token_of(currency, v->spec);
    Py py;
    bool by_bucks = currency == 1 && py.ge(p.bucks, I(*price));
    if (py.failed()) {
      *raised = py.raised();
      return std::nullopt;
    }
    if (by_bucks) {
      p.bucks = py.sub(p.bucks, I(*price), true);
    } else if (currency == 2 && p.spend_cc(cc_price(*price))) {
    } else {
      bool paid = false;
      if (token) {
        V* st = summit::state(py, p);
        const V at = I(static_cast<int64_t>(*token));
        bool positive = st && py.gt(py.getitem(py.getitem(*st, V::string("tokens")), at), I(0));
        if (positive) {
          st = summit::state(py, p);
          V& tokens = *st->get("tokens");
          V less = py.sub(py.getitem(tokens, at), I(1), true);
          py.setitem(tokens, at, std::move(less));
          paid = true;
        }
        if (py.failed()) {
          *raised = py.raised();
          return std::nullopt;
        }
      }
      if (!paid) return fail;
    }
  }
  std::vector<int64_t> s = cars::slots(*c, *v, parts[1], parts[3], parts[5]);
  Bytes chosen;
  if (!owned) {
    for (Bytes& cfg : *configs) cfg = cars::with_state(cfg, 0);
    std::optional<Bytes> cfg = cars::config(*c, variant, s, static_cast<int64_t>(configs->size()) + 1, raised);
    if (!cfg) return std::nullopt;
    configs->push_back(std::move(*cfg));
    chosen = configs->back();
    std::stable_partition(garage->begin(), garage->end(),
                          [model](const std::pair<V, std::vector<Bytes>>& e) { return obj::eq(e.first, U(model)); });
    cars::store(p, *garage);
  } else {
    chosen = (*configs)[*owned];
    if (!pack::unpack_check(chosen, 0x23, 76, raised)) return std::nullopt;
    for (size_t i = 0; i < cars::SLOT_COUNT; i++) {
      s[i] = static_cast<int64_t>(chosen[0x23 + 2 * i]) << 8 | chosen[0x23 + 2 * i + 1];
    }
  }
  wire::put_u64(r.element, model);
  append(r.element, cars::array(1, cars::with_state(chosen, cars::BOUGHT)));
  listed = cars::part_list(s, raised);
  if (!listed) return std::nullopt;
  append(r.element, *listed);
  r.changed = true;
  return r;
}

HandlerResult spec_purchase(Ctx& ctx, const Bytes& body, const Registered&) {
  Player& p = ctx.player;
  Raised raised;
  std::optional<SpecResult> r = spec_element(ctx, body, &raised);
  if (!r) return HandlerResult::fail(raised);
  if (r->changed) {
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  pack::Packer pk;
  pk.H(0).I(p.bucks).I(p.cc()).I(0);
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  Bytes out = std::move(pk.out());
  append(out, cars::array(1, r->element));
  for (uint8_t b : {1, 0, 0, 0, 0, 0, 3}) out.push_back(b);
  return HandlerResult::reply(0x05E0DCFF, std::move(out));
}

struct MountRequest {
  uint64_t model = 0;
  uint64_t spec = 0;
  std::vector<std::pair<uint16_t, uint16_t>> pairs;
};

std::optional<MountRequest> mount_request(const Bytes& body) {
  Raised ignored;
  uint32_t n = 0;
  if (!pack::unpack_u32(body, 0x1C, &n, &ignored)) return std::nullopt;
  size_t o = 0x20 + 12, end = 0x20 + static_cast<size_t>(n);
  while (o + 16 <= end) {
    uint32_t cn = 0;
    if (!pack::unpack_u32(body, o, &cn, &ignored)) return std::nullopt;
    Bytes car = slice(body, o + 16, o + cn + 4);
    o += static_cast<size_t>(cn) + 4;
    if (car.size() < 8) return std::nullopt;
    uint64_t model = wire::get_u64(car.data());
    size_t d = 8 + 4 + 12, dend = car.size();
    while (d + 16 <= dend) {
      uint32_t dn = 0;
      if (!pack::unpack_u32(car, d, &dn, &ignored)) return std::nullopt;
      Bytes dress = slice(car, d + 16, d + dn + 4);
      d += static_cast<size_t>(dn) + 4;
      if (dress.size() < 8) return std::nullopt;
      MountRequest r;
      r.model = model;
      r.spec = wire::get_u64(dress.data());
      uint32_t count = 0;
      if (!pack::unpack_u32(dress, 8 + 4 + 12, &count, &ignored)) return std::nullopt;
      if (count > 0 && 8 + 4 + 16 + 4 * static_cast<size_t>(count) > dress.size()) return std::nullopt;
      for (size_t k = 0; k < count; k++) {
        const uint8_t* q = dress.data() + 8 + 4 + 16 + 4 * k;
        r.pairs.emplace_back(static_cast<uint16_t>(q[0] << 8 | q[1]), static_cast<uint16_t>(q[2] << 8 | q[3]));
      }
      return r;
    }
  }
  return std::nullopt;
}

bool part_owned(const Bytes& bits, uint32_t x, bool* out, Raised* raised) {
  uint32_t n = 0;
  if (!pack::unpack_u32(bits, 16, &n, raised)) return false;
  *out = false;
  if ((x >> 6) >= n) return true;
  size_t at = 20 + 8 * static_cast<size_t>(x >> 6);
  if (!pack::unpack_check(bits, at, 8, raised)) return false;
  *out = (wire::get_u64(bits.data() + at) >> (x & 63) & 1) != 0;
  return true;
}

HandlerResult mount_reply(uint8_t ok) { return HandlerResult::reply(0x3BBE49A4, Bytes{ok, 0, 0}); }

HandlerResult mount_parts(Ctx& ctx, const Bytes& body, const Registered&) {
  Player& p = ctx.player;
  std::optional<MountRequest> req = mount_request(body);
  if (!req || req->pairs.empty()) return mount_reply(1);
  Raised raised;
  std::optional<cars::Garage> garage = cars::entries(p, &raised);
  if (!garage) return HandlerResult::fail(raised);
  std::vector<Bytes> none;
  std::vector<Bytes>* configs = configs_of(*garage, req->model);
  if (!configs) configs = &none;
  std::optional<size_t> k;
  for (size_t i = 0; i < configs->size(); i++) {
    std::optional<uint64_t> s = cars::spec((*configs)[i], &raised);
    if (!s) return HandlerResult::fail(raised);
    if (*s == req->spec) {
      k = i;
      break;
    }
  }
  if (!k) return mount_reply(0);
  Bytes cfg = (*configs)[*k];
  if (!pack::unpack_check(cfg, 0x23, 76, &raised)) return HandlerResult::fail(raised);
  uint16_t slots[cars::SLOT_COUNT];
  for (size_t i = 0; i < cars::SLOT_COUNT; i++) {
    slots[i] = static_cast<uint16_t>(cfg[0x23 + 2 * i] << 8 | cfg[0x24 + 2 * i]);
  }
  auto dlc73 = p.dlc.find(73);
  Py py;
  size_t owned_n = py.len(p.owned_items);
  Bytes owned_words;
  for (const V& w : py.iter(p.owned_items)) {
    if (!py.pack(owned_words, 'Q', w)) break;
  }
  if (py.failed()) return HandlerResult::fail(py.raised());
  std::vector<Bytes> fields{dlc73 != p.dlc.end() ? dlc73->second : cars::array(0, Bytes()),
                            cars::array(static_cast<uint32_t>(owned_n), owned_words)};
  std::optional<Bytes> player_parts = cars::player_parts(*garage, fields, &raised);
  if (!player_parts) return HandlerResult::fail(raised);
  uint32_t first = 0;
  if (!pack::unpack_u32(cfg, 0x6F, &first, &raised)) return HandlerResult::fail(raised);
  Bytes own = slice(cfg, 0x6F + 4 + static_cast<size_t>(first), cfg.size());
  std::vector<std::pair<uint16_t, uint16_t>> wanted;
  for (const auto& sp : req->pairs) {
    auto it = std::find_if(wanted.begin(), wanted.end(),
                           [&sp](const std::pair<uint16_t, uint16_t>& w) { return w.first == sp.first; });
    if (it == wanted.end()) {
      wanted.push_back(sp);
    } else {
      it->second = sp.second;
    }
  }
  bool changed = false;
  for (const auto& sp : wanted) {
    uint16_t slot = sp.first, part = sp.second;
    if (slot >= cars::SLOT_COUNT) continue;
    const Bytes& bits = slot < cars::OWNED_SLOTS ? own : *player_parts;
    bool ok = part == 0xFFFF;
    if (!ok && !part_owned(bits, part, &ok, &raised)) return HandlerResult::fail(raised);
    if (ok) {
      slots[slot] = part;
      changed = true;
    }
  }
  if (!changed) return mount_reply(0);
  for (size_t i = 0; i < cars::SLOT_COUNT; i++) {
    cfg[0x23 + 2 * i] = static_cast<uint8_t>(slots[i] >> 8);
    cfg[0x24 + 2 * i] = static_cast<uint8_t>(slots[i]);
  }
  (*configs)[*k] = std::move(cfg);
  cars::store(p, *garage);
  if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  return mount_reply(1);
}

HandlerResult car_purchase(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = request;
  if (body.size() < 0x25 + 80) body.resize(0x25 + 80, 0);
  Player& p = ctx.player;
  const cars::Catalog* c = cars::catalog(*ctx.core);
  Raised raised;
  uint8_t currency = body[0x24];
  uint64_t q[7];
  for (size_t i = 0; i < 7; i++) q[i] = wire::get_u64(body.data() + 0x25 + 8 * i);
  uint64_t variant = q[0], paint = q[1], part = q[4], livery = q[6];
  const cars::Variant* v = c ? c->variant(variant) : nullptr;
  Bytes out;
  auto head = [&](uint16_t status, const V& model) -> bool {
    pack::Packer pk;
    pk.H(status).I(p.bucks).I(p.cc()).I(0);
    pk.raw(Bytes());
    if (!pk.failed()) {
      Py q;
      if (!q.pack(pk.out(), 'q', model)) {
        raised = q.raised();
        return false;
      }
    }
    if (pk.failed()) {
      raised = pk.raised();
      return false;
    }
    out = std::move(pk.out());
    return true;
  };
  if (!v) {
    if (!head(0xFFFF, I(-1))) return HandlerResult::fail(raised);
    append(out, cars::array(0, Bytes()));
    append(out, cars::array(0, Bytes()));
    return HandlerResult::reply(0x4239B014, std::move(out));
  }
  uint64_t model = v->model;
  std::optional<int64_t> price = c->price(model);
  std::vector<int64_t> s = cars::slots(*c, *v, paint, part, livery);
  std::optional<cars::Garage> garage = cars::entries(p, &raised);
  if (!garage) return HandlerResult::fail(raised);
  std::vector<Bytes>* configs = configs_of(*garage, model);
  std::optional<Bytes> cfg = cars::config(*c, variant, s, cars::BOUGHT, &raised);
  if (!cfg) return HandlerResult::fail(raised);
  bool ok = false;
  if (!price || dlc_only(*c, *v, p) || garage_full(*garage, configs)) {
  } else if (currency == 1) {
    Py py;
    V cost = car_price(py, *price, py.getitem(p.perks, I(static_cast<int64_t>(CAR_DISCOUNT_PERK))));
    bool covers = py.ge(p.bucks, cost);
    if (py.failed()) return HandlerResult::fail(py.raised());
    if (covers) {
      p.bucks = py.sub(p.bucks, cost, true);
      if (py.failed()) return HandlerResult::fail(py.raised());
      ok = true;
    }
  } else if (currency == 2 && p.spend_cc(cc_price(*price))) {
    ok = true;
  }
  if (ok) {
    if (!configs) {
      garage->emplace_back(U(model), std::vector<Bytes>());
      configs = &garage->back().second;
    }
    for (Bytes& x : *configs) x = cars::with_state(x, 0);
    if (configs->size() + 1 > 255) return HandlerResult::fail(Raised{"ValueError", "bytes must be in range(0, 256)"});
    configs->push_back(cars::with_state(*cfg, static_cast<uint8_t>(configs->size() + 1)));
    cars::store(p, *garage);
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  if (!head(ok ? 0 : 0xFFFF, ok ? U(model) : I(-1))) return HandlerResult::fail(raised);
  append(out, cars::array(1, cars::with_state(*cfg, cars::BOUGHT)));
  std::optional<Bytes> listed = cars::part_list(s, &raised);
  if (!listed) return HandlerResult::fail(raised);
  append(out, *listed);
  return HandlerResult::reply(0x4239B014, std::move(out));
}

struct NpcItem {
  int64_t catalog = 0;
  int64_t price = 0;
  bool has_model = false;
  uint64_t model = 0;
  bool config = false;
};

const std::unordered_map<std::string, NpcItem>* npc_items() {
  static const std::unique_ptr<std::unordered_map<std::string, NpcItem>> items =
      []() -> std::unique_ptr<std::unordered_map<std::string, NpcItem>> {
    const json::Value* t = gamedata::table("npc.json");
    const json::Value* list = t ? t->get("items") : nullptr;
    if (!list || !list->is_object()) return nullptr;
    std::unique_ptr<std::unordered_map<std::string, NpcItem>> out(new std::unordered_map<std::string, NpcItem>());
    for (const json::Member& m : list->members()) {
      NpcItem e;
      const json::Value* cat = m.value.get("catalog");
      const json::Value* price = m.value.get("price");
      const json::Value* model = m.value.get("model");
      const json::Value* config = m.value.get("config");
      e.catalog = cat && cat->as_i64() ? *cat->as_i64() : 0;
      e.price = price && price->as_i64() ? *price->as_i64() : 0;
      e.has_model = model && model->is_string() && !model->as_string().empty();
      e.model = e.has_model ? gamedata::hex_id(model->as_string()) : 0;
      e.config = config && truthy(*config);
      (*out)[m.key] = e;
    }
    return out;
  }();
  return items.get();
}

std::optional<int64_t> zone_start(uint64_t spec) {
  switch (spec) {
    case 0x37568E:
    case 0x375691:
      return 450;
    case 0x375693:
      return 1125;
    case 0x375692:
      return 2200;
    case 0x375690:
      return 3700;
    case 0x37568F:
    case 0x7938315:
    case 0x7938316:
    case 0x7938317:
      return 6000;
    default:
      return std::nullopt;
  }
}

std::optional<NpcItem> tuner_item(const cars::Catalog* c, uint64_t item_id, uint64_t model, uint64_t spec,
                                  const std::vector<V>& models) {
  std::optional<int64_t> price = zone_start(spec);
  if (!c || c->part(item_id) == cars::NO_PART || !has_model(models, model) || !price) return std::nullopt;
  NpcItem e;
  e.catalog = 1;
  e.price = *price;
  e.has_model = true;
  e.model = model;
  return e;
}

bool npc_list(const Bytes& body, size_t o, std::vector<std::array<uint64_t, 3>>* out, size_t* end) {
  Raised ignored;
  uint32_t n = 0, count = 0;
  if (!pack::unpack_u32(body, o, &n, &ignored)) return false;
  if (!pack::unpack_u32(body, o + 16, &count, &ignored)) return false;
  if (count > 0 && o + 20 + 0x19 * static_cast<size_t>(count - 1) + 24 > body.size()) return false;
  for (size_t k = 0; k < count; k++) {
    const uint8_t* e = body.data() + o + 20 + 0x19 * k;
    out->push_back({wire::get_u64(e), wire::get_u64(e + 8), wire::get_u64(e + 16)});
  }
  *end = o + 4 + static_cast<size_t>(n);
  return true;
}

HandlerResult npc_purchase(Ctx& ctx, const Bytes& body, const Registered&) {
  Player& p = ctx.player;
  std::vector<std::array<uint64_t, 3>> first, second;
  size_t end = 0, ignored_end = 0;
  if (!npc_list(body, 0x1C, &first, &end) || !npc_list(body, end, &second, &ignored_end)) {
    first.clear();
    second.clear();
  }
  const cars::Catalog* c = cars::catalog(*ctx.core);
  Raised raised;
  std::optional<cars::Garage> garage = cars::entries(p, &raised);
  if (!garage) return HandlerResult::fail(raised);
  Py py;
  std::vector<V> models;
  if (!model_set(py, *garage, &models)) return HandlerResult::fail(py.raised());
  const std::unordered_map<std::string, NpcItem>* npc = npc_items();
  bool bought = false;
  for (int currency : {1, 2}) {
    for (const auto& entry : currency == 1 ? first : second) {
      uint64_t item_id = entry[0], a = entry[1], b = entry[2];
      std::optional<NpcItem> item;
      if (npc) {
        auto it = npc->find(gamedata::hex_key(item_id));
        if (it != npc->end()) item = it->second;
      }
      if (!item) item = tuner_item(c, item_id, a, b, models);
      if (!item || item->config || (item->has_model && !has_model(models, item->model))) continue;
      if (item->catalog == 2 && !has_model(models, a)) continue;
      int64_t price = currency == 1 ? item->price : cc_price(item->price);
      if (currency == 1) {
        bool short_of = py.lt(p.bucks, I(price));
        if (py.failed()) return HandlerResult::fail(py.raised());
        if (short_of) continue;
        p.bucks = py.sub(p.bucks, I(price), true);
      } else if (!p.spend_cc(price)) {
        continue;
      }
      int64_t part = c ? c->part(item_id) : cars::NO_PART;
      if (part != cars::NO_PART) set_bit(py, p.owned_items, static_cast<uint64_t>(part));
      if (py.failed()) return HandlerResult::fail(py.raised());
      bought = true;
    }
  }
  if (bought) {
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  pack::Packer pk;
  pk.H(0).I(p.bucks).I(p.cc()).I(0);
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  return HandlerResult::reply(0x27A7C7FE, std::move(pk.out()));
}

void add(std::vector<Registered>& out, uint32_t id, const char* name, Handler fn) {
  Registered r;
  r.id = id;
  r.name = name;
  r.fn = fn;
  out.push_back(std::move(r));
}

}

void register_garage(std::vector<Registered>& out) {
  add(out, 0x68B6EEC5, "CAR_REPORT", car_report);
  add(out, 0x1F86C435, "PEER_REGISTER", peer_register);
  add(out, 0xF70D87D3, "SPEC_PURCHASE", spec_purchase);
  add(out, 0x7DC1F4F0, "MOUNT_PARTS", mount_parts);
  add(out, 0xBBF8C44B, "CAR_PURCHASE", car_purchase);
  add(out, 0xF93D21DA, "NPC_PURCHASE", npc_purchase);
}

}
}
