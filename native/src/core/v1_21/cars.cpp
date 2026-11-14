#include "core/v1_21/cars.h"

#include <algorithm>
#include <memory>
#include <set>

#include "core/core.h"
#include "core/json.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/summit.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {
namespace cars {

namespace {

using gamedata::hex_of;
using gamedata::int_of;

std::unique_ptr<Catalog> from_json(const json::Value& d) {
  std::unique_ptr<Catalog> c(new Catalog());
  if (const json::Value* variants = d.get("variants")) {
    for (const json::Member& m : variants->members()) {
      Variant v;
      v.model = hex_of(m.value.get("model"));
      v.spec = hex_of(m.value.get("spec"));
      v.default_part = hex_of(m.value.get("default_part"));
      v.tier = int_of(m.value.get("tier"));
      v.slots = gamedata::ints_of(m.value.get("slots"));
      v.slots.resize(SLOT_COUNT, NO_PART);
      c->variants[gamedata::hex_id(m.key)] = std::move(v);
    }
  }
  if (const json::Value* parts = d.get("parts")) {
    for (const json::Member& m : parts->members()) c->parts[gamedata::hex_id(m.key)] = int_of(&m.value);
  }
  if (const json::Value* prices = d.get("prices")) {
    for (const json::Member& m : prices->members()) c->prices[gamedata::hex_id(m.key)] = int_of(&m.value);
  }
  c->budget = int_of(d.get("budget"));
  c->dealer = hex_of(d.get("dealer"));
  c->slot_defaults = gamedata::ints_of(d.get("slot_defaults"));
  if (const json::Value* sp = d.get("spec_prices")) {
    for (const json::Member& m : sp->members()) c->spec_prices[gamedata::hex_id(m.key)] = gamedata::ints_of(&m.value);
  }
  if (const json::Value* ss = d.get("special_specs")) {
    for (const json::Value& x : ss->items()) c->special_specs.push_back(hex_of(&x));
  }
  return c;
}

const Catalog* embedded() {
  static const std::unique_ptr<Catalog> c = []() -> std::unique_ptr<Catalog> {
    const json::Value* t = gamedata::table("cars.json");
    if (!t || !t->is_object()) return nullptr;
    return from_json(*t);
  }();
  return c.get();
}

bool split_configs(const Bytes& raw, uint32_t count, std::vector<Bytes>* out, Raised* raised) {
  size_t o = 0;
  for (uint32_t k = 0; k < count; k++) {
    size_t start = o;
    o += 0x6F;
    for (int arr = 0; arr < 2; arr++) {
      uint32_t n = 0;
      if (!pack::unpack_u32(raw, o, &n, raised)) return false;
      o += 4 + n;
    }
    o += 1;
    size_t a = std::min(start, raw.size()), b = std::min(o, raw.size());
    out->emplace_back(raw.begin() + a, raw.begin() + std::max(a, b));
  }
  return true;
}

Raised index_error() { return Raised{"IndexError", "list index out of range"}; }

}

int64_t Catalog::part(uint64_t pid) const {
  auto it = parts.find(pid);
  return it == parts.end() ? NO_PART : it->second;
}

const Variant* Catalog::variant(uint64_t id) const {
  auto it = variants.find(id);
  return it == variants.end() ? nullptr : &it->second;
}

std::optional<int64_t> Catalog::price(uint64_t model) const {
  auto it = prices.find(model);
  if (it == prices.end()) return std::nullopt;
  return it->second;
}

bool Catalog::special(uint64_t s) const {
  return std::find(special_specs.begin(), special_specs.end(), s) != special_specs.end();
}

const Catalog* catalog(const Core& core) {
  if (!core.overrides().car_catalog) return nullptr;
  return embedded();
}

const std::vector<size_t>& list_slots() {
  static const std::vector<size_t> s = [] {
    std::vector<size_t> out;
    for (size_t i = 11; i < 19; i++) out.push_back(i);
    for (size_t i = 28; i < 38; i++) out.push_back(i);
    for (size_t i : {19, 20, 21}) out.push_back(i);
    for (size_t i = 23; i < 28; i++) out.push_back(i);
    return out;
  }();
  return s;
}

const Bytes& empty_config() {
  static const Bytes c = [] {
    Bytes out(0x6F, 0);
    Bytes first = wire::blob(wire::frame(wire::T_ARRAY, Bytes(4, 0)));
    out.insert(out.end(), first.begin(), first.end());
    for (uint32_t w : {0x10u, 0x10u, wire::T_ARRAY, 0x21000000u, 0u}) wire::put_u32(out, w);
    out.push_back(BOUGHT);
    return out;
  }();
  return c;
}

Bytes array(uint32_t count, const Bytes& items) {
  Bytes body;
  wire::put_u32(body, count);
  body.insert(body.end(), items.begin(), items.end());
  return wire::blob(wire::frame(wire::T_ARRAY, body));
}

std::vector<int64_t> slots(const Catalog& c, const Variant& v, uint64_t a, uint64_t b, uint64_t d) {
  std::vector<int64_t> s = v.slots;
  if (d != NONE) s[20] = c.part(d);
  if (a != NONE) s[21] = c.part(a);
  s[22] = s[21];
  s[24] = c.part(b == NONE ? v.default_part : b);
  return s;
}

std::optional<Bytes> config(const Catalog& c, uint64_t variant, const std::vector<int64_t>& s, int64_t state,
                            Raised* raised) {
  const Variant* v = c.variant(variant);
  if (!v) {
    *raised = Raised{"KeyError", py::uint_to_text(variant)};
    return std::nullopt;
  }
  std::vector<uint64_t> owned(OWNED_WORDS, 0);
  for (size_t i = 0; i < OWNED_SLOTS && i < s.size(); i++) {
    int64_t x = s[i];
    if (x == NO_PART) continue;
    int64_t w = x >> 6;
    if (w < 0) w += static_cast<int64_t>(OWNED_WORDS);
    if (w < 0 || w >= static_cast<int64_t>(OWNED_WORDS)) {
      *raised = index_error();
      return std::nullopt;
    }
    owned[static_cast<size_t>(w)] |= uint64_t(1) << (static_cast<uint64_t>(x) & 63);
  }
  Bytes out;
  wire::put_u64(out, v->spec);
  wire::put_u64(out, variant);
  out.push_back(1);
  out.push_back(0x64);
  out.resize(out.size() + 17, 0);
  if (s.size() != SLOT_COUNT) {
    *raised = pack::error("pack expected 38 items for packing (got " + py::uint_to_text(s.size()) + ")");
    return std::nullopt;
  }
  for (int64_t x : s) {
    if (!pack::put(out, 'H', pack::Num::of_signed(x), raised)) return std::nullopt;
  }
  Bytes defaults;
  for (int64_t x : c.slot_defaults) {
    if (!pack::put(defaults, 'I', pack::Num::of_signed(x), raised)) return std::nullopt;
  }
  Bytes a = array(static_cast<uint32_t>(c.slot_defaults.size()), defaults);
  out.insert(out.end(), a.begin(), a.end());
  Bytes words;
  for (uint64_t w : owned) wire::put_u64(words, w);
  Bytes b = array(OWNED_WORDS, words);
  out.insert(out.end(), b.begin(), b.end());
  if (state < 0 || state > 255) {
    *raised = Raised{"ValueError", "bytes must be in range(0, 256)"};
    return std::nullopt;
  }
  out.push_back(static_cast<uint8_t>(state));
  return out;
}

std::optional<Bytes> part_list(const std::vector<int64_t>& s, Raised* raised) {
  std::vector<int64_t> shown;
  for (size_t i : list_slots()) {
    if (i >= s.size()) {
      *raised = index_error();
      return std::nullopt;
    }
    if (s[i] != NO_PART) shown.push_back(s[i]);
  }
  Bytes items;
  for (int64_t x : shown) {
    if (!pack::put(items, 'H', pack::Num::of_signed(x), raised)) return std::nullopt;
  }
  return array(static_cast<uint32_t>(shown.size()), items);
}

std::optional<Bytes> garage_field(const Garage& entries, Raised* raised) {
  Bytes items;
  obj::Py py;
  for (const auto& e : entries) {
    if (!py.pack(items, 'Q', e.first)) {
      *raised = py.raised();
      return std::nullopt;
    }
    Bytes cs;
    for (const Bytes& c : e.second) cs.insert(cs.end(), c.begin(), c.end());
    Bytes inner = array(static_cast<uint32_t>(e.second.size()), cs);
    items.insert(items.end(), inner.begin(), inner.end());
  }
  return array(static_cast<uint32_t>(entries.size()), items);
}

std::optional<Garage> parse_garage(const Bytes& field, Raised* raised) {
  Garage out;
  uint32_t n = 0;
  if (!pack::unpack_u32(field, 16, &n, raised)) return std::nullopt;
  size_t o = 20;
  for (uint32_t k = 0; k < n; k++) {
    uint64_t model = 0;
    uint32_t size = 0;
    if (!pack::unpack_check(field, o, 12, raised)) return std::nullopt;
    model = wire::get_u64(field.data() + o);
    size = wire::get_u32(field.data() + o + 8);
    size_t a = std::min(o + 12, field.size()), b = std::min(o + 12 + size, field.size());
    Bytes inner(field.begin() + a, field.begin() + std::max(a, b));
    uint32_t count = 0;
    if (!pack::unpack_u32(inner, 12, &count, raised)) return std::nullopt;
    Bytes raw(inner.begin() + std::min<size_t>(16, inner.size()), inner.end());
    std::vector<Bytes> configs;
    if (!split_configs(raw, count, &configs, raised)) return std::nullopt;
    out.emplace_back(obj::U(model), std::move(configs));
    o += 12 + size;
  }
  return out;
}

std::optional<uint64_t> spec(const Bytes& cfg, Raised* raised) {
  if (!pack::unpack_check(cfg, 0, 8, raised)) return std::nullopt;
  return wire::get_u64(cfg.data());
}

std::optional<uint64_t> variant_of(const Bytes& cfg, Raised* raised) {
  if (!pack::unpack_check(cfg, 8, 8, raised)) return std::nullopt;
  return wire::get_u64(cfg.data() + 8);
}

Bytes with_state(const Bytes& cfg, uint8_t state) {
  Bytes out(cfg.begin(), cfg.empty() ? cfg.end() : cfg.end() - 1);
  out.push_back(state);
  return out;
}

std::optional<Bytes> player_parts(const Garage& entries, const std::vector<Bytes>& fields, Raised* raised) {
  std::vector<uint64_t> words(PLAYER_PARTS, 0);
  for (const Bytes& f : fields) {
    uint32_t n = 0;
    if (!pack::unpack_u32(f, 16, &n, raised)) return std::nullopt;
    if (!pack::unpack_check(f, 20, static_cast<size_t>(n) * 8, raised)) return std::nullopt;
    for (size_t i = 0; i < n && i < PLAYER_PARTS; i++) words[i] |= wire::get_u64(f.data() + 20 + 8 * i);
  }
  for (const auto& e : entries) {
    for (const Bytes& cfg : e.second) {
      if (cfg.size() < 0x23 + 76) continue;
      for (size_t s = OWNED_SLOTS; s < SLOT_COUNT; s++) {
        uint32_t x = static_cast<uint32_t>(cfg[0x23 + 2 * s]) << 8 | cfg[0x23 + 2 * s + 1];
        if (x != 0xFFFF && (x >> 6) < PLAYER_PARTS) words[x >> 6] |= uint64_t(1) << (x & 63);
      }
    }
  }
  Bytes items;
  for (uint64_t w : words) wire::put_u64(items, w);
  return array(PLAYER_PARTS, items);
}

std::optional<Garage> entries(const Player& p, Raised* raised) {
  obj::Py py;
  const V* own72 = p.extra_field(72);
  if (own72 && !truthy(p.garage_all)) {
    std::optional<Bytes> field = py.fromhex(*own72);
    if (!field) {
      *raised = py.raised();
      return std::nullopt;
    }
    return parse_garage(*field, raised);
  }
  Garage own;
  for (const V& g : py.iter(p.garage)) {
    V model = py.getitem(g, V::string("model"));
    std::vector<Bytes> cs;
    for (const V& c : py.iter(py.getitem(g, V::string("configs")))) {
      std::optional<Bytes> b = py.fromhex(c);
      if (!b) break;
      cs.push_back(std::move(*b));
    }
    if (py.failed()) break;
    own.emplace_back(std::move(model), std::move(cs));
  }
  if (py.failed()) {
    *raised = py.raised();
    return std::nullopt;
  }
  auto dlc72 = p.dlc.find(72);
  if (dlc72 == p.dlc.end()) return own;
  bool done = p.prologue_done(py);
  if (py.failed()) {
    *raised = py.raised();
    return std::nullopt;
  }
  if (!done) return own;
  std::optional<Garage> packs = parse_garage(dlc72->second, raised);
  if (!packs) return std::nullopt;
  for (const auto& o : own) {
    if (!obj::hashable(o.first)) {
      *raised = Raised{"TypeError", std::string("unhashable type: '") + obj::type_name(o.first) + "'"};
      return std::nullopt;
    }
  }
  Garage out;
  for (auto& e : *packs) {
    bool owned = false;
    for (const auto& o : own) owned = owned || obj::same(o.first, e.first);
    if (!owned) out.push_back(std::move(e));
  }
  for (auto& o : own) out.push_back(std::move(o));
  return out;
}

std::vector<Bytes>* configs_of(Garage& garage, const V& model) {
  for (auto& e : garage) {
    if (obj::eq(e.first, model)) return &e.second;
  }
  return nullptr;
}

void store(Player& p, const Garage& garage) {
  V g = V::array();
  for (const auto& e : garage) {
    V configs = V::array();
    for (const Bytes& c : e.second) configs.push(V::string(py::hex(c)));
    V entry = V::object();
    entry.set("model", e.first);
    entry.set("configs", std::move(configs));
    g.push(std::move(entry));
  }
  p.garage = std::move(g);
  p.garage_all = obj::B(true);
  p.extra_fields.erase(std::remove_if(p.extra_fields.begin(), p.extra_fields.end(),
                                      [](const std::pair<std::string, V>& e) { return e.first == "72"; }),
                       p.extra_fields.end());
}

bool spec_price(const Catalog& c, const Garage& garage, const Variant& v, std::optional<int64_t>* price,
                Raised* raised) {
  std::set<uint64_t> owned;
  for (const auto& e : garage) {
    for (const Bytes& cfg : e.second) {
      std::optional<uint64_t> s = spec(cfg, raised);
      if (!s) return false;
      owned.insert(*s);
    }
  }
  bool charged = false;
  if (c.special(v.spec)) {
    for (uint64_t s : c.special_specs) charged = charged || owned.count(s) != 0;
  } else {
    charged = owned.count(v.spec) != 0;
  }
  price->reset();
  auto it = c.spec_prices.find(v.spec);
  size_t n = it == c.spec_prices.end() ? 0 : it->second.size();
  if (!charged || !(0 < v.tier && static_cast<size_t>(v.tier) <= n)) return true;
  *price = it->second[static_cast<size_t>(v.tier - 1)];
  return true;
}

}
}
}
