#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <unordered_map>

#include "core/core.h"
#include "core/v1_21/cars.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/mission.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/physparts.h"
#include "core/v1_21/rewards.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

using obj::I;
using obj::Py;
using obj::U;
using pack::Packer;

HandlerResult failed(const Raised& r) { return HandlerResult::fail(r); }

Raised key_error(const std::string& key) { return Raised{"KeyError", py::repr(key)}; }

bool check(const Bytes& b, size_t offset, size_t size, Raised* r) { return pack::unpack_check(b, offset, size, r); }
uint16_t u16(const Bytes& b, size_t o) { return static_cast<uint16_t>(b[o] << 8 | b[o + 1]); }
uint32_t u32(const Bytes& b, size_t o) { return wire::get_u32(b.data() + o); }
uint64_t u64(const Bytes& b, size_t o) { return wire::get_u64(b.data() + o); }

Bytes ljust(const Bytes& b, size_t n) {
  Bytes out = b;
  if (out.size() < n) out.resize(n, 0);
  return out;
}

void append(Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); }

HandlerResult reply_of(uint32_t type, Packer& pk) {
  if (pk.failed()) return failed(pk.raised());
  return HandlerResult::reply(type, std::move(pk.out()));
}

const rewards::Tables* reward_tables(const Ctx& ctx) { return rewards::tables(*ctx.core); }
const cars::Catalog* car_catalog(const Ctx& ctx) { return cars::catalog(*ctx.core); }

bool read_array(const Bytes& body, size_t pos, size_t item_size, std::vector<Bytes>* items, size_t* next,
                Raised* r) {
  if (!check(body, pos, 4, r)) return false;
  uint32_t size = u32(body, pos);
  if (!check(body, pos + 4 + 12, 4, r)) return false;
  uint64_t count = u32(body, pos + 4 + 12);
  size_t start = pos + 4 + 16;
  uint64_t fits = body.size() >= start ? (body.size() - start) / item_size : 0;
  count = std::min(count, fits);
  for (uint64_t i = 0; i < count; i++) {
    size_t a = start + static_cast<size_t>(i) * item_size;
    items->emplace_back(body.begin() + a, body.begin() + a + item_size);
  }
  *next = pos + 4 + size;
  return true;
}

HandlerResult stats_write(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  std::vector<Bytes> stats, records;
  size_t pos = 0, ignored = 0;
  if (!read_array(body, 8, 8, &stats, &pos, &r)) return failed(r);
  if (!read_array(body, pos, 24, &records, &ignored, &r)) return failed(r);
  Player& p = ctx.player;
  Py py;
  for (const Bytes& item : stats) p.set_stat(py, u32(item, 0), u32(item, 4));
  for (const Bytes& item : records) {
    p.set_record_field(py, u64(item, 0), u64(item, 8), u32(item, 16), u32(item, 20));
    if (py.failed()) return failed(py.raised());
  }
  if (std::optional<Raised> f = p.save()) return failed(*f);
  return HandlerResult::reply(0xF63E32AE, Bytes{0x01});
}

bool pack_all(Py& py, Bytes& out, const V& values, size_t n) {
  if (!values.is_array() && !values.is_object() && !values.is_string()) {
    return py.raise("TypeError", std::string("Value after * must be an iterable, not ") + obj::type_name(values));
  }
  std::vector<V> xs = py.iter(values);
  if (xs.size() != n) {
    return py.raise("struct.error", "pack expected " + std::to_string(n) + " items for packing (got " +
                                        std::to_string(xs.size()) + ")");
  }
  Bytes packed;
  for (const V& x : xs) {
    if (!py.pack(packed, 'I', x)) return false;
  }
  append(out, packed);
  return true;
}

HandlerResult stats_get(Ctx& ctx, const Bytes&, const Registered&) {
  Py py;
  const Player& p = ctx.player;
  Bytes records;
  for (const Record& rec : p.records) {
    Bytes ab;
    if (!py.pack(ab, 'Q', rec.a) || !py.pack(ab, 'Q', rec.b)) return failed(py.raised());
    append(records, ab);
    if (!pack_all(py, records, rec.fields, RECORD_FIELDS)) return failed(py.raised());
  }
  Bytes out;
  wire::put_u64(out, ctx.player_id);
  V stats = V::array();
  for (size_t i = 0; i < STAT_COUNT && i < p.stats.size(); i++) stats.push(p.stats[i]);
  if (!pack_all(py, out, stats, STAT_COUNT)) return failed(py.raised());
  Bytes arr;
  wire::put_u32(arr, static_cast<uint32_t>(p.records.size()));
  append(arr, records);
  append(out, wire::blob(wire::frame(wire::T_ARRAY, arr)));
  append(out, wire::empty_array());
  return HandlerResult::reply(0xFE559B32, std::move(out));
}

constexpr int64_t PERK_POINT_BUCKS = 100000, PERK_POINT_CREDITS = 2000;
constexpr int64_t RESET_PER_LEVEL = 100;
constexpr int64_t FIRST_BUCKS = 2000, FIRST_XP = 25;

void add_bucks_any(Py& py, Ctx& ctx, Player& p, int64_t d) {
  if (const rewards::Tables* t = reward_tables(ctx)) {
    rewards::add_bucks(py, *t, p, I(d));
  } else {
    p.bucks = py.max(I(0), py.add(p.bucks, I(d)));
  }
}

void add_xp_any(Py& py, Ctx& ctx, Player& p, int64_t d) {
  if (const rewards::Tables* t = reward_tables(ctx)) {
    rewards::add_xp(py, *t, p, I(d), ctx.core->module_random());
  } else {
    p.xp = py.add(p.xp, I(d), true);
  }
}

HandlerResult perks(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x3F);
  Player& p = ctx.player;
  Py py;
  for (int i = 0; i < body[0x20]; i++) {
    bool covers = py.ge(p.bucks, I(PERK_POINT_BUCKS));
    if (py.failed()) return failed(py.raised());
    if (covers) {
      p.bucks = py.sub(p.bucks, I(PERK_POINT_BUCKS), true);
      p.perk_points += 1;
    }
  }
  for (int i = 0; i < body[0x1E]; i++) {
    if (p.spend_cc(PERK_POINT_CREDITS)) p.perk_points += 1;
  }
  bool ok = true;
  if (body[0x1C] == 1) {
    V cost = py.mul(I(RESET_PER_LEVEL), py.sum(p.perks));
    bool covers = py.ge(p.bucks, cost);
    if (py.failed()) return failed(py.raised());
    if (covers) {
      p.bucks = py.sub(p.bucks, cost, true);
      if (py.failed()) return failed(py.raised());
      p.perks = obj::list_of(std::vector<int64_t>(PERKS, 0));
    } else {
      ok = false;
    }
  } else {
    std::vector<int64_t> levels;
    int64_t sum = 0;
    for (size_t i = 0; i < PERKS; i++) {
      int64_t x = std::min<int64_t>(body[0x21 + i], PERK_MAX);
      levels.push_back(x);
      sum += x;
    }
    p.perks = obj::list_of(levels);
    if (sum > p.perk_points) {
      p.perks = obj::list_of(std::vector<int64_t>(PERKS, 0));
      ok = false;
    }
  }
  if (std::optional<Raised> f = p.save()) return failed(*f);
  Packer pk;
  pk.B(ok ? 1 : 0).I(p.bucks).I(p.cc()).H(p.perk_points).H(0);
  return reply_of(0xB37AA94C, pk);
}

HandlerResult first_reward(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x3F);
  Player& p = ctx.player;
  Py py;
  bool fresh = set_bit(py, p.rewarded, body[0x1D]);
  if (py.failed()) return failed(py.raised());
  if (fresh) {
    add_bucks_any(py, ctx, p, FIRST_BUCKS);
    add_xp_any(py, ctx, p, FIRST_XP);
    if (py.failed()) return failed(py.raised());
    if (std::optional<Raised> f = p.save()) return failed(*f);
  }
  std::optional<Bytes> words = pack::u64_array(py, p.rewarded);
  if (!words) return failed(py.raised());
  Packer pk;
  pk.raw(Bytes{1}).raw(*words).B(p.level).I(p.xp).I(p.bucks).H(p.perk_points).H(0);
  return reply_of(0x884B40AE, pk);
}

HandlerResult table_bit(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x3F);
  Player& p = ctx.player;
  Py py;
  bool fresh = set_bit(py, p.table_bits, u16(body, 0x1C));
  if (py.failed()) return failed(py.raised());
  if (fresh) {
    if (std::optional<Raised> f = p.save()) return failed(*f);
  }
  std::optional<Bytes> words = pack::u64_array(py, p.table_bits);
  if (!words) return failed(py.raised());
  Bytes out{1};
  append(out, *words);
  out.push_back(0);
  out.push_back(0);
  return HandlerResult::reply(0xA7B8025B, std::move(out));
}

constexpr uint32_t T_COLLECTIBLE = 0x9234FC36;
constexpr size_t MAX_COLLECTIBLES = 4096;

struct Sets {
  std::unordered_map<std::string, std::string> set_of;
  std::unordered_map<std::string, int64_t> needed;
};

const Sets* sets() {
  static const std::unique_ptr<Sets> loaded = []() -> std::unique_ptr<Sets> {
    const json::Value* t = gamedata::table("collectibles.json");
    if (!t || !t->is_object()) return nullptr;
    std::unique_ptr<Sets> s(new Sets());
    if (const json::Value* of = t->get("set_of")) {
      for (const json::Member& m : of->members()) s->set_of[m.key] = m.value.is_string() ? m.value.as_string() : "";
    }
    if (const json::Value* needed = t->get("needed")) {
      for (const json::Member& m : needed->members()) s->needed[m.key] = m.value.as_i64().value_or(0);
    }
    return s;
  }();
  return loaded.get();
}

const std::string* set_of(const Sets& s, const std::string& key) {
  auto it = s.set_of.find(key);
  return it == s.set_of.end() ? nullptr : &it->second;
}

void complete_set(Py& py, Ctx& ctx, Player& p, uint64_t item) {
  const Sets* s = sets();
  const cars::Catalog* c = car_catalog(ctx);
  if (!s || !c) return;
  const std::string* group = set_of(*s, gamedata::hex_key(item));
  if (!group) return;
  int64_t done = 0;
  for (const V& e : py.iter(p.collectibles)) {
    std::vector<V> iv;
    if (!py.unpack(e, 2, &iv)) return;
    std::string key = py.format_hex(iv[0]);
    if (py.failed()) return;
    const std::string* g = set_of(*s, key);
    if (!g || *g != *group) continue;
    Bytes word;
    if (!py.pack(word, 'I', iv[1])) return;
    if (pack::f32_at(word, 0) >= 1.0) done++;
  }
  if (py.failed()) return;
  int64_t part = c->part(gamedata::hex_id(*group));
  auto needed = s->needed.find(*group);
  if (needed == s->needed.end()) {
    py.raise(key_error(*group));
    return;
  }
  if (done >= needed->second && part != 0xFFFF) set_bit(py, p.owned_items, static_cast<uint64_t>(part));
}

HandlerResult collectibles(Ctx& ctx, const Bytes& body, const Registered&) {
  Player& p = ctx.player;
  std::vector<std::pair<uint64_t, uint32_t>> entries;
  if (body.size() >= 8 + 16 + 4) {
    uint64_t count = u32(body, 8 + 16);
    uint64_t fits = body.size() >= 28 ? (body.size() - 28) / 12 : 0;
    count = std::min(count, fits);
    for (uint64_t k = 0; k < count; k++) {
      size_t o = 8 + 20 + 12 * static_cast<size_t>(k);
      entries.emplace_back(u64(body, o), u32(body, o + 8));
    }
  }
  Py py;
  std::vector<std::pair<V, size_t>> known;
  auto find = [&known](const V& key) -> std::pair<V, size_t>* {
    for (auto& e : known) {
      if (obj::same(e.first, key)) return &e;
    }
    return nullptr;
  };
  {
    std::vector<V> list = py.iter(p.collectibles);
    for (size_t k = 0; k < list.size(); k++) {
      std::vector<V> iv;
      if (!py.unpack(list[k], 2, &iv)) break;
      if (!obj::hashable(iv[0])) {
        py.raise("TypeError", std::string("unhashable type: '") + obj::type_name(iv[0]) + "'");
        break;
      }
      if (auto* have = find(iv[0])) {
        have->second = k;
      } else {
        known.emplace_back(iv[0], k);
      }
    }
    if (py.failed()) return failed(py.raised());
  }
  for (const auto& e : entries) {
    const V item = U(e.first);
    if (auto* have = find(item)) {
      V* entry = py.item_ref(p.collectibles, I(static_cast<int64_t>(have->second)));
      if (!entry) return failed(py.raised());
      if (!py.setitem(*entry, I(1), U(e.second))) return failed(py.raised());
    } else {
      size_t n = py.len(p.collectibles);
      if (py.failed()) return failed(py.raised());
      if (n < MAX_COLLECTIBLES) {
        if (!p.collectibles.is_array()) {
          return failed(Raised{"AttributeError", std::string("'") + obj::type_name(p.collectibles) +
                                                     "' object has no attribute 'append'"});
        }
        known.emplace_back(item, n);
        V pair = V::array();
        pair.push(item);
        pair.push(U(e.second));
        p.collectibles.push(std::move(pair));
      }
    }
  }
  std::set<std::string> groups;
  const Sets* s = sets();
  for (const auto& e : entries) {
    const std::string* group = s ? set_of(*s, gamedata::hex_key(e.first)) : nullptr;
    if (group && !groups.count(*group)) {
      groups.insert(*group);
      complete_set(py, ctx, p, e.first);
      if (py.failed()) return failed(py.raised());
    }
  }
  if (!entries.empty()) {
    if (std::optional<Raised> f = p.save()) return failed(*f);
  }
  return HandlerResult::reply(0x48A252AB, Bytes{0x01});
}

HandlerResult collectible_list(Ctx& ctx, const Bytes&, const Registered&) {
  Py py;
  std::vector<Bytes> items;
  for (const V& e : py.iter(ctx.player.collectibles)) {
    std::vector<V> iv;
    if (!py.unpack(e, 2, &iv)) break;
    Bytes item;
    if (!py.pack(item, 'Q', iv[0]) || !py.pack(item, 'I', iv[1])) break;
    items.push_back(wire::frame(T_COLLECTIBLE, item));
  }
  if (py.failed()) return failed(py.raised());
  Bytes out;
  wire::put_u64(out, ctx.player_id);
  append(out, wire::blob(wire::serializer_list(items)));
  return HandlerResult::reply(0x573316DD, std::move(out));
}

struct Regions {
  int64_t threshold = 0;
  std::vector<std::string> bits;
  std::vector<std::string> regions;
  std::unordered_map<std::string, std::string> cars;
};

const Regions* regions() {
  static const std::unique_ptr<Regions> loaded = []() -> std::unique_ptr<Regions> {
    const json::Value* t = gamedata::table("regions.json");
    if (!t || !t->is_object()) return nullptr;
    std::unique_ptr<Regions> g(new Regions());
    if (const json::Value* th = t->get("threshold")) g->threshold = th->as_i64().value_or(0);
    auto strings = [](const json::Value* v, std::vector<std::string>* out) {
      if (!v) return;
      for (const json::Value& x : v->items()) out->push_back(x.is_string() ? x.as_string() : "");
    };
    strings(t->get("bits"), &g->bits);
    strings(t->get("regions"), &g->regions);
    if (const json::Value* c = t->get("cars")) {
      for (const json::Member& m : c->members()) g->cars[m.key] = m.value.is_string() ? m.value.as_string() : "";
    }
    return g;
  }();
  return loaded.get();
}

constexpr uint8_t REWARD_STATE = 5;

HandlerResult region_reward(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x1D);
  Player& p = ctx.player;
  const cars::Catalog* c = car_catalog(ctx);
  const Regions* g = regions();
  size_t idx = body[0x1C];
  Raised r;
  Py py;
  if (g && c && idx < g->regions.size()) {
    const std::string& key = g->regions[idx];
    int64_t have = 0;
    for (size_t n = 0; n < g->bits.size(); n++) {
      if (g->bits[n] != key) continue;
      size_t len = py.len(p.table_bits);
      if (py.failed()) return failed(py.raised());
      if ((n >> 6) >= len) continue;
      V bit = py.band(py.rshift(py.getitem(p.table_bits, U(n >> 6)), U(n & 63)), I(1));
      if (py.failed()) return failed(py.raised());
      if (obj::truthy(bit)) have++;
    }
    auto car = g->cars.find(key);
    if (car == g->cars.end()) return failed(key_error(key));
    uint64_t variant = gamedata::hex_id(car->second);
    std::optional<cars::Garage> garage = cars::entries(p, &r);
    if (!garage) return failed(r);
    bool owned = false;
    for (const auto& e : *garage) {
      for (const Bytes& cfg : e.second) {
        std::optional<uint64_t> v = cars::variant_of(cfg, &r);
        if (!v) return failed(r);
        if (*v == variant) {
          owned = true;
          break;
        }
      }
      if (owned) break;
    }
    const cars::Variant* var = c->variant(variant);
    if (have >= g->threshold && var && !owned) {
      const V model = U(var->model);
      std::vector<int64_t> s = cars::slots(*c, *var, cars::NONE, cars::NONE, cars::NONE);
      std::vector<Bytes>* configs = cars::configs_of(*garage, model);
      if (!configs) {
        garage->emplace_back(model, std::vector<Bytes>());
        configs = &garage->back().second;
      }
      std::optional<Bytes> cfg = cars::config(*c, variant, s, cars::STORED, &r);
      if (!cfg) return failed(r);
      configs->push_back(*cfg);
      cars::store(p, *garage);
      if (std::optional<Raised> f = p.save()) return failed(*f);
      Bytes out;
      wire::put_u16(out, 0);
      wire::put_u64(out, var->model);
      append(out, cars::array(1, cars::with_state(*cfg, REWARD_STATE)));
      std::optional<Bytes> parts = cars::part_list(s, &r);
      if (!parts) return failed(r);
      append(out, *parts);
      return HandlerResult::reply(0x6939A37D, std::move(out));
    }
  }
  const gamedata::ReplyTemplate* t = gamedata::reply_template(0x88648CE9);
  if (!t) return failed(key_error("0x88648ce9"));
  return HandlerResult::reply(0x6939A37D, t->body);
}

constexpr int64_t SKILL_CAP = 100000;

HandlerResult skill_result(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!check(body, 0x1C, 4, &r)) return failed(r);
  const V value = I(u32(body, 0x1C));
  Player& p = ctx.player;
  Py py;
  bool below = py.le(p.skill_last, value);
  V delta = py.sub(value, below ? p.skill_last : I(0));
  bool ok = py.le(delta, I(SKILL_CAP));
  if (py.failed()) return failed(py.raised());
  if (ok) {
    delta = rewards::allow(py, *ctx.core, p, ctx.now_us(), I(0), delta).second;
    if (const rewards::Tables* t = reward_tables(ctx)) {
      rewards::add_bucks(py, *t, p, delta);
    } else {
      p.bucks = py.add(p.bucks, delta, true);
    }
    if (py.failed()) return failed(py.raised());
    p.skill_last = value;
  } else {
    p.skill_last = I(0);
  }
  if (std::optional<Raised> f = p.save()) return failed(*f);
  Packer pk;
  pk.B(ok ? 1 : 0).I(ctx.player.bucks).H(0);
  return reply_of(0xCD89472D, pk);
}

HandlerResult map_state(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!check(body, 0x1C + 4 + 12, 4, &r)) return failed(r);
  uint32_t count = u32(body, 0x1C + 4 + 12);
  if (count < 11) {
    size_t at = 0x1C + 4 + 16;
    if (!check(body, at, 8 * static_cast<size_t>(count), &r)) return failed(r);
    std::vector<uint64_t> words;
    for (uint32_t i = 0; i < count; i++) words.push_back(u64(body, at + 8 * i));
    ctx.player.map_state = obj::list_of_u64(words);
    if (std::optional<Raised> f = ctx.player.save()) return failed(*f);
  }
  Bytes out{1};
  wire::put_u16(out, 0);
  return HandlerResult::reply(0x49440B69, std::move(out));
}

HandlerResult player_update(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!check(body, 0x20, 4, &r)) return failed(r);
  size_t n = u32(body, 0x20);
  Player& p = ctx.player;
  if (!check(body, 0x2D + n, 16, &r)) return failed(r);
  uint64_t a = u64(body, 0x2D + n), b = u64(body, 0x2D + n + 8);
  if (a != 0xFFFFFFFFFFFFFFFFull && a != 0xFFFFFFFFull) p.ids = obj::list_of_u64({a, b});
  if (!check(body, 0x3E + n, 12, &r)) return failed(r);
  std::vector<uint64_t> pos{u32(body, 0x3E + n), u32(body, 0x3E + n + 4), u32(body, 0x3E + n + 8)};
  if (pos[0] && pos[1] && pos[2]) p.position = obj::list_of_u64(pos);
  if (!check(body, 0x50 + n, 12, &r)) return failed(r);
  std::vector<uint64_t> direction{u32(body, 0x50 + n), u32(body, 0x50 + n + 4), u32(body, 0x50 + n + 8)};
  if (direction[0] && direction[1] && direction[2]) p.direction = obj::list_of_u64(direction);
  if (!check(body, 0x78 + n, 4, &r)) return failed(r);
  p.zone = I(u32(body, 0x78 + n));
  if (!check(body, 0x60 + n, 4, &r)) return failed(r);
  const V spent = I(u32(body, 0x60 + n));
  Py py;
  if (obj::truthy(spent)) {
    bool less = py.lt(spent, p.update_charge);
    if (py.failed()) return failed(py.raised());
    if (less) {
      p.update_charge = spent;
    } else if (!obj::eq(spent, p.update_charge)) {
      bool covers = py.ge(p.bucks, py.sub(spent, p.update_charge));
      if (py.failed()) return failed(py.raised());
      if (covers) {
        p.bucks = py.sub(p.bucks, py.sub(spent, p.update_charge), true);
        if (py.failed()) return failed(py.raised());
        p.update_charge = spent;
      }
    }
  }
  if (std::optional<Raised> f = p.save()) return failed(*f);
  Bytes out{0};
  append(out, wire::empty_array());
  append(out, wire::empty_array());
  out.push_back(0);
  return HandlerResult::reply(0xC8A3B105, std::move(out));
}

bool is_dealer(const Ctx& ctx, uint64_t mission) {
  const cars::Catalog* c = car_catalog(ctx);
  return c && mission == c->dealer;
}

Bytes no_car() {
  Bytes out;
  wire::put_u64(out, 0xFFFFFFFFFFFFFFFFull);
  append(out, wire::empty_array());
  append(out, wire::empty_array());
  wire::put_u16(out, 0x20);
  return out;
}

Bytes car_part(uint64_t model, const std::vector<Bytes>& shown, const Bytes& parts) {
  Bytes out;
  wire::put_u64(out, model);
  Bytes joined;
  for (const Bytes& c : shown) append(joined, c);
  append(out, cars::array(static_cast<uint32_t>(shown.size()), joined));
  append(out, parts);
  wire::put_u16(out, 0);
  return out;
}

bool dealer_car(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* r) {
  const cars::Catalog* c = car_catalog(ctx);
  if (!check(body, 0x3A, 8, r)) return false;
  uint64_t variant = u64(body, 0x3A);
  if (!c || variant == cars::NONE) return true;
  const cars::Variant* v = c->variant(variant);
  uint64_t model = v ? v->model : cars::NONE;
  auto price = c->prices.find(model);
  int64_t bucks = std::max<int64_t>(0, c->budget - (price == c->prices.end() ? 0 : price->second));
  Py py;
  if (const rewards::Tables* t = reward_tables(ctx)) {
    rewards::apply(py, *t, p, I(0), I(bucks), ctx.core->module_random());
  } else {
    p.bucks = py.add(p.bucks, I(bucks), true);
  }
  if (py.failed()) {
    *r = py.raised();
    return false;
  }
  if (!v) {
    std::optional<Bytes> parts = cars::part_list(std::vector<int64_t>(cars::SLOT_COUNT, 0), r);
    if (!parts) return false;
    Bytes out;
    wire::put_u64(out, 0xFFFFFFFFFFFFFFFFull);
    append(out, wire::empty_array());
    append(out, *parts);
    wire::put_u16(out, 0);
    *car = std::move(out);
    return true;
  }
  uint64_t abd[3];
  const size_t offsets[3] = {0x52, 0x62, 0x72};
  for (int i = 0; i < 3; i++) {
    if (!check(body, offsets[i], 8, r)) return false;
    abd[i] = u64(body, offsets[i]);
  }
  std::vector<int64_t> s = cars::slots(*c, *v, abd[0], abd[1], abd[2]);
  std::optional<cars::Garage> garage = cars::entries(p, r);
  if (!garage) return false;
  std::vector<Bytes>* configs = cars::configs_of(*garage, U(model));
  if (!configs) {
    garage->emplace_back(U(model), std::vector<Bytes>());
    configs = &garage->back().second;
  }
  std::optional<Bytes> cfg = cars::config(*c, variant, s, cars::STORED, r);
  if (!cfg) return false;
  configs->push_back(std::move(*cfg));
  cars::store(p, *garage);
  std::vector<Bytes> shown(configs->begin(), configs->end() - 1);
  shown.push_back(cars::with_state(configs->back(), cars::NEW));
  std::optional<Bytes> parts = cars::part_list(s, r);
  if (!parts) return false;
  *car = car_part(model, shown, *parts);
  return true;
}

bool owned_dealer_car(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* r) {
  const cars::Catalog* c = car_catalog(ctx);
  if (!check(body, 0x3A, 8, r)) return false;
  uint64_t variant = u64(body, 0x3A);
  const cars::Variant* v = c ? c->variant(variant) : nullptr;
  if (!v) return true;
  uint64_t model = v->model;
  std::optional<cars::Garage> garage = cars::entries(p, r);
  if (!garage) return false;
  const std::vector<Bytes>* configs = cars::configs_of(*garage, U(model));
  if (!configs || configs->empty()) return true;
  size_t k = configs->size();
  for (size_t i = 0; i < configs->size(); i++) {
    std::optional<uint64_t> vo = cars::variant_of((*configs)[i], r);
    if (!vo) return false;
    if (*vo == variant) {
      k = i;
      break;
    }
  }
  std::vector<Bytes> shown;
  if (k == configs->size()) {
    k = configs->size() - 1;
    shown = *configs;
  } else {
    for (size_t i = 0; i < configs->size(); i++) {
      shown.push_back(i == k ? cars::with_state((*configs)[i], cars::NEW) : (*configs)[i]);
    }
  }
  const Bytes& cfg = (*configs)[k];
  if (!check(cfg, 0x23, 2 * cars::SLOT_COUNT, r)) return false;
  std::vector<int64_t> s;
  for (size_t i = 0; i < cars::SLOT_COUNT; i++) s.push_back(u16(cfg, 0x23 + 2 * i));
  std::optional<Bytes> parts = cars::part_list(s, r);
  if (!parts) return false;
  *car = car_part(model, shown, *parts);
  return true;
}

std::optional<int64_t> reward_part(Ctx& ctx, Player& p, uint64_t mission, int64_t score, const Bytes& body,
                                   Raised* r) {
  const rewards::Tables* t = reward_tables(ctx);
  if (!t) return -1;
  auto it = t->missions.find(mission);
  if (it == t->missions.end() || it->second.thresholds.size() < 4 || body.size() < 0x38) return -1;
  const rewards::Mission& m = it->second;
  int64_t md = -1;
  if (score >= m.thresholds[0]) {
    std::optional<int64_t> got = rewards::medal(*t, m.thresholds, score, p.level, r);
    if (!got) return std::nullopt;
    md = *got;
  }
  uint64_t model = u64(body, 0x28), spec = u64(body, 0x30);
  std::optional<int64_t> part;
  if (!physparts::mission_part(p, mission, md, model, spec, ctx.core->module_random(), *ctx.core, &part, r)) {
    return std::nullopt;
  }
  return part ? *part : -1;
}

HandlerResult mission_end(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!check(body, 0x1C, 12, &r)) return failed(r);
  uint64_t mission = u64(body, 0x1C);
  int64_t score = u32(body, 0x24);
  Player& p = ctx.player;
  std::optional<bool> dealer = pays_dealer(ctx, p, mission, &r);
  if (!dealer) return failed(r);
  if (!finish_mission(*ctx.core, p, mission, score, ctx.now_us(), true, &r)) return failed(r);
  std::optional<int64_t> part = reward_part(ctx, p, mission, score, body, &r);
  if (!part) return failed(r);
  std::optional<Bytes> car;
  if (*dealer) {
    if (!dealer_car(ctx, p, body, &car, &r)) return failed(r);
  } else if (is_dealer(ctx, mission)) {
    if (!owned_dealer_car(ctx, p, body, &car, &r)) return failed(r);
  }
  if (std::optional<Raised> f = p.save()) return failed(*f);
  if (!car) car = no_car();
  Bytes granted = wire::empty_array();
  if (*part >= 0 && physparts::ANNOUNCE_PART) {
    const physparts::Table* t = physparts::table();
    if (!t || static_cast<size_t>(*part) >= t->ids.size()) return failed(Raised{"IndexError", "list index out of range"});
    granted = wire::u64_array({t->ids[static_cast<size_t>(*part)]});
  }
  Packer pk;
  pk.B(1).Q(mission).I(p.level).I(p.xp).I(p.bucks).I(p.cc()).I(p.perk_points);
  if (pk.failed()) return failed(pk.raised());
  Py py;
  std::optional<Bytes> abilities = pack::u64_array(py, p.abilities);
  if (!abilities) return failed(py.raised());
  pk.raw(granted).raw(*abilities).raw(*car).raw(wire::empty_array()).I(0);
  return reply_of(0x02459285, pk);
}

HandlerResult progress_bit(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!check(body, 0x1C, 2, &r)) return failed(r);
  Py py;
  ctx.player.set_progress_bit(py, u16(body, 0x1C));
  if (py.failed()) return failed(py.raised());
  if (std::optional<Raised> f = ctx.player.save()) return failed(*f);
  std::optional<Bytes> words = pack::u64_array(py, ctx.player.progress);
  if (!words) return failed(py.raised());
  Bytes out{1};
  append(out, *words);
  out.push_back(0);
  out.push_back(0);
  return HandlerResult::reply(0xCD246F74, std::move(out));
}

HandlerResult player_ratings(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised r;
  if (!check(body, 28 + 4 + 12, 4, &r)) return failed(r);
  uint32_t count = u32(body, 28 + 4 + 12);
  size_t at = 28 + 4 + 16;
  if (!check(body, at, 8 * static_cast<size_t>(count), &r)) return failed(r);
  std::vector<Bytes> per_player;
  for (uint32_t i = 0; i < count; i++) {
    uint64_t pid = u64(body, at + 8 * i);
    if (pid != ctx.player_id) continue;
    Bytes arr;
    wire::put_u32(arr, 4);
    arr.resize(arr.size() + 2 * 29, 0);
    Bytes one = rating(pid);
    append(arr, one);
    append(arr, one);
    per_player.push_back(wire::blob(wire::frame(wire::T_ARRAY, arr)));
  }
  Bytes arr;
  wire::put_u32(arr, static_cast<uint32_t>(per_player.size()));
  for (const Bytes& b : per_player) append(arr, b);
  Bytes out{1};
  append(out, wire::blob(wire::frame(wire::T_ARRAY, arr)));
  return HandlerResult::reply(0x752FDA89, std::move(out));
}

void add(std::vector<Registered>& out, uint32_t id, const char* name, Handler fn) {
  Registered reg;
  reg.id = id;
  reg.name = name;
  reg.fn = fn;
  out.push_back(std::move(reg));
}

}

Bytes rating(uint64_t player_id) {
  static const uint8_t kRest[] = {0x40, 0x41, 0xf0, 0x7c, 0x41, 0x05, 0x55, 0x55, 0xff, 0xff, 0xff,
                                  0xff, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  Bytes out;
  wire::put_u64(out, player_id);
  out.insert(out.end(), kRest, kRest + sizeof(kRest));
  return out;
}

bool finish_mission(Core& core, Player& p, uint64_t mission, int64_t score, int64_t now_us, bool paid,
                    Raised* raised) {
  Py py;
  p.complete_mission(py, mission);
  if (py.failed()) {
    *raised = py.raised();
    return false;
  }
  const rewards::Tables* t = rewards::tables(core);
  if (!t) return true;
  if (paid) {
    std::optional<std::pair<int64_t, int64_t>> reward = rewards::mission_reward(*t, mission, score, p.level, raised);
    if (!reward) return false;
    std::pair<V, V> allowed = rewards::allow(py, core, p, now_us, I(reward->first), I(reward->second));
    rewards::apply(py, *t, p, allowed.first, allowed.second, core.module_random());
  }
  std::vector<uint64_t> earned = rewards::abilities(py, *t, p.missions_done);
  if (py.failed()) {
    *raised = py.raised();
    return false;
  }
  p.abilities = truthy(p.free_roam) ? obj::list_of_u64({ALL_ABILITIES[0], ALL_ABILITIES[1]}) : obj::list_of_u64(earned);
  return true;
}

bool dealer_mission(const Ctx& ctx, uint64_t mission) { return is_dealer(ctx, mission); }

std::optional<bool> pays_dealer(const Ctx& ctx, const Player& p, uint64_t mission, Raised* raised) {
  if (!is_dealer(ctx, mission)) return false;
  Py py;
  bool done = py.contains(p.missions_done, U(mission));
  if (py.failed()) {
    *raised = py.raised();
    return std::nullopt;
  }
  return !done;
}

bool dealer_car_part(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* raised) {
  return dealer_car(ctx, p, body, car, raised);
}

bool owned_dealer_car_part(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* raised) {
  return owned_dealer_car(ctx, p, body, car, raised);
}

bool mission_reward_part(Ctx& ctx, Player& p, uint64_t mission, int64_t score, const Bytes& body,
                         std::optional<int64_t>* part, Raised* raised) {
  std::optional<int64_t> got = reward_part(ctx, p, mission, score, body, raised);
  if (!got) return false;
  *part = *got >= 0 ? got : std::nullopt;
  return true;
}

void register_mission(std::vector<Registered>& out) {
  add(out, 0xBB6835CF, "MISSION_END", mission_end);
  add(out, 0xC43C7F34, "PROGRESS_BIT", progress_bit);
  add(out, 0xE20F5542, "PLAYER_RATINGS", player_ratings);
}

void register_presence(std::vector<Registered>& out) {
  add(out, 0x49466F25, "PLAYER_UPDATE", player_update);
}

void register_progress(std::vector<Registered>& out) {
  add(out, 0xBF9B8FBD, "PERKS", perks);
  add(out, 0xE6D82939, "FIRST_REWARD", first_reward);
  add(out, 0x7C044567, "TABLE_BIT", table_bit);
  add(out, 0x740437D6, "COLLECTIBLES", collectibles);
  add(out, 0x2FC9E8A6, "COLLECTIBLE_LIST", collectible_list);
  add(out, 0x88648CE9, "REGION_REWARD", region_reward);
}

void register_skill(std::vector<Registered>& out) {
  add(out, 0xF16252BC, "SKILL_RESULT", skill_result);
  add(out, 0x37D6825B, "MAP_STATE", map_state);
}

void register_stats(std::vector<Registered>& out) {
  add(out, 0x2E0D6D99, "STATS_WRITE", stats_write);
  add(out, 0x812D4694, "STATS_GET", stats_get);
}

}
}
