#include "core/v1_00/handlers.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "core/core.h"
#include "core/v1_00/cars.h"
#include "core/v1_00/translate.h"
#include "core/v1_21/cars.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/mission.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/physparts.h"
#include "core/v1_21/relay.h"
#include "core/wire.h"

namespace tcr {
namespace v1_00 {

namespace {

using v1_21::Ctx;
using v1_21::HandleResult;
using v1_21::Player;
using obj::V;
namespace cars121 = v1_21::cars;
namespace gamedata = v1_21::gamedata;
namespace pack = v1_21::pack;

HandleResult reply(uint32_t type, const Bytes& body) {
  HandleResult r;
  r.kind = HandleResult::kFrame;
  r.frame = wire::frame(type, body);
  return r;
}

HandleResult fail(const Raised& raised) {
  HandleResult r;
  r.kind = HandleResult::kRaised;
  r.raised = raised;
  return r;
}

void append(Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); }

Bytes slice(const Bytes& b, size_t start, size_t stop) {
  start = std::min(start, b.size());
  stop = std::min(stop, b.size());
  if (stop <= start) return Bytes();
  return Bytes(b.begin() + static_cast<std::ptrdiff_t>(start), b.begin() + static_cast<std::ptrdiff_t>(stop));
}

bool check(const Bytes& b, size_t offset, size_t size, Raised* r) { return pack::unpack_check(b, offset, size, r); }
uint64_t u64(const Bytes& b, size_t o) { return wire::get_u64(b.data() + o); }
uint32_t u32(const Bytes& b, size_t o) { return wire::get_u32(b.data() + o); }
bool put(Bytes& out, char code, int64_t v, Raised* r) { return pack::put(out, code, pack::Num::of_signed(v), r); }

bool base_reply(Ctx& ctx, uint32_t req, const Bytes& body, Bytes* inner, HandleResult* stop) {
  HandleResult h = v1_21::handle(ctx, req, body);
  switch (h.kind) {
    case HandleResult::kFrame:
      *inner = std::move(h.frame);
      return true;
    case HandleResult::kNoReply:
      inner->clear();
      return true;
    case HandleResult::kUnknown:
      *stop = fail(Raised{"TypeError", "'NoneType' object is not subscriptable"});
      return false;
    default:
      *stop = std::move(h);
      return false;
  }
}

struct Auto {
  uint32_t reply = 0;
  std::vector<std::string> layout;
  std::string name;
  std::vector<int64_t> none;
};

const std::map<uint32_t, Auto>& autos() {
  static const std::map<uint32_t, Auto> table = [] {
    std::map<uint32_t, Auto> out;
    const json::Value* t = gamedata::table("v1_00_replies.json");
    if (!t || !t->is_object()) return out;
    for (const json::Member& m : t->members()) {
      if (!m.key.empty() && m.key[0] == '_') continue;
      Auto a;
      const json::Value* rid = m.value.get("reply_id");
      a.reply = static_cast<uint32_t>(gamedata::hex_id(rid && rid->is_string() ? rid->as_string() : ""));
      if (const json::Value* layout = m.value.get("layout")) {
        for (const json::Value& tok : layout->items()) a.layout.push_back(tok.as_string());
      }
      const json::Value* req = m.value.get("request");
      std::string name = req && req->is_string() ? req->as_string() : "";
      a.name = name.substr(0, name.find('('));
      uint32_t id = static_cast<uint32_t>(gamedata::hex_id(m.key));
      if (const json::Value* none = m.value.get("none"); none && id != 0x0BE1ACD4) {
        for (const json::Value& k : none->items()) a.none.push_back(k.as_i64().value_or(-1));
      }
      out[id] = std::move(a);
    }
    return out;
  }();
  return table;
}

const Auto* auto_of(uint32_t id) {
  auto it = autos().find(id);
  return it == autos().end() ? nullptr : &it->second;
}

const std::unordered_map<uint32_t, std::vector<std::string>>& schema121() {
  static const std::unordered_map<uint32_t, std::vector<std::string>> table = [] {
    std::unordered_map<uint32_t, std::vector<std::string>> out;
    const json::Value* t = gamedata::table("replies_schema.json");
    if (!t || !t->is_object()) return out;
    for (const json::Member& m : t->members()) {
      if (!m.key.empty() && m.key[0] == '_') continue;
      std::vector<std::string> layout;
      if (const json::Value* l = m.value.get("layout")) {
        for (const json::Value& tok : l->items()) layout.push_back(tok.as_string());
      }
      out[static_cast<uint32_t>(gamedata::hex_id(m.key))] = std::move(layout);
    }
    return out;
  }();
  return table;
}

const std::pair<uint32_t, uint32_t> kAliases[] = {
    {0x8711A3A7, 0x9FD2D3D4},
    {0x3CDDE44F, 0x49466F25},
};

uint32_t alias_of(uint32_t id) {
  for (const auto& a : kAliases) {
    if (a.first == id) return a.second;
  }
  return id;
}

const std::vector<std::string>* layouts121(uint32_t reply_id) {
  static const std::vector<std::string> set_group{"u8", "u16"};
  static const std::vector<std::string> group_status{"u64", "u8", "u64*2", "u32*2", "u32*5", "u32*12", "u64*25"};
  if (reply_id == 0x3069959F) return &set_group;
  if (reply_id == 0x3D3D4841) return &group_status;
  return nullptr;
}

translate::Mapping pick(size_t n, const std::map<size_t, size_t>& pairs) {
  translate::Mapping out(n);
  for (const auto& p : pairs) {
    if (p.first < n) out[p.first] = p.second;
  }
  return out;
}

const translate::Mapping* override_of(uint32_t id) {
  static const translate::Mapping change_faction = pick(19, {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {15, 4}});
  static const translate::Mapping meta = [] {
    std::map<size_t, size_t> p{{0, 0}, {1, 1}, {2, 3}, {14, 2}};
    for (size_t k = 0; k < 25; k++) p[18 + k] = 23 + k;
    return pick(43, p);
  }();
  static const translate::Mapping salary = [] {
    std::map<size_t, size_t> p{{0, 0}, {1, 1}, {2, 2}, {3, 51}, {16, 20}, {17, 21}, {18, 23}, {30, 22}};
    for (size_t k = 0; k < 25; k++) p[34 + k] = 26 + k;
    return pick(62, p);
  }();
  switch (id) {
    case 0x7932CE3E:
      return &change_faction;
    case 0x82B2A771:
      return &meta;
    case 0xA7476B7D:
      return &salary;
    default:
      return nullptr;
  }
}

bool auto_payload(const Auto& a, Bytes* out, Raised* r) {
  std::vector<std::string> tokens = translate::expand(a.layout);
  std::vector<translate::Value> values;
  for (size_t k = 0; k < tokens.size(); k++) {
    bool none = std::find(a.none.begin(), a.none.end(), static_cast<int64_t>(k)) != a.none.end();
    values.push_back(none ? translate::Value::of(~uint64_t(0)) : translate::Value::none());
  }
  return translate::encode(tokens, values, out, r);
}

HandleResult auto_reply(const Auto& a) {
  Bytes payload;
  Raised r;
  if (!auto_payload(a, &payload, &r)) return fail(r);
  return reply(a.reply, payload);
}

std::optional<uint32_t> answered_by_121(uint32_t id) {
  uint32_t req121 = alias_of(id);
  if (auto_of(id) && (v1_21::registered(req121) || gamedata::auto_reply(req121))) return req121;
  return std::nullopt;
}

const std::vector<std::string>* layout_121(uint32_t reply_id, uint32_t req121) {
  auto it = schema121().find(reply_id);
  if (it != schema121().end()) return &it->second;
  const gamedata::AutoReply* a = gamedata::auto_reply(req121);
  if (a && a->reply == reply_id) return &a->layout;
  return layouts121(reply_id);
}

HandleResult translated(Ctx& ctx, uint32_t id, uint32_t req121, const Bytes& body) {
  const Auto& a = *auto_of(id);
  HandleResult inner = v1_21::handle(ctx, req121, body);
  if (inner.kind != HandleResult::kFrame) return inner;
  uint32_t rid121 = u32(inner.frame, 4);
  if (rid121 == a.reply) return inner;
  const std::vector<std::string>* layout121 = layout_121(rid121, req121);
  if (!layout121) return auto_reply(a);
  Bytes payload;
  Raised r;
  if (!translate::transcode(slice(inner.frame, 12, inner.frame.size()), *layout121, a.layout, override_of(id),
                            &payload, &r)) {
    if (r.type != "struct.error") return fail(r);
    ctx.core->env().print_line("[1.00] " + name_of(id) + ": the 1.21 reply does not fit its layout (" + r.text +
                               "); sent the default reply");
    return auto_reply(a);
  }
  return reply(a.reply, payload);
}

using Own = HandleResult (*)(Ctx& ctx, const Bytes& body);

Bytes str_blob(const std::string& s) { return wire::blob(Bytes(s.begin(), s.end())); }

constexpr uint32_t T_NET_VALUES = 0x56F3387D;
constexpr uint32_t T_NET_VALUE_INT = 0x564E9254;
constexpr uint32_t T_CONFIG_SECTION = 0x5999B98B;
constexpr uint32_t T_CONFIG_VALUE = 0xBA2D37DC;

Bytes net_values() {
  Bytes allow = str_blob("ALLOW_PLAY_WITHOUT_PUNCH");
  append(allow, str_blob("1"));
  Bytes wait = str_blob("MAX_LOGIN_WAIT_TIME_MS");
  wire::put_u32(wait, 5000);
  Bytes section = str_blob("Punch");
  append(section, wire::blob(wire::serializer_list(
                      {wire::frame(T_CONFIG_VALUE, allow), wire::frame(T_NET_VALUE_INT, wait)})));
  return wire::frame(T_NET_VALUES, wire::blob(wire::serializer_list({wire::frame(T_CONFIG_SECTION, section)})));
}

HandleResult load_entity_read_only(Ctx&, const Bytes&) {
  Bytes out{1};
  wire::put_u64(out, 0);
  append(out, wire::blob(net_values()));
  wire::put_u32(out, 0);
  return reply(0xCE8FE9D1, out);
}

enum Kind { U8, U16, U32, U64, F32, GUID, STR, ARRAY, U8X30 };
const Kind kFields121[] = {
    U8,  U64, U64, U64, GUID, STR, U8, U8, U32, U32, U32, ARRAY, U32, U16, U64, U32, U64, U64, F32, F32,
    F32, F32, F32, F32, ARRAY, ARRAY, ARRAY, ARRAY, ARRAY, ARRAY, ARRAY, U32, U8, U8X30, U32, U32, U32, ARRAY,
    U8,  U16, U8, U8, U8, U8, U64, U64, U8, U32, U64, U64, U8, U8, U8, U64, ARRAY, U8, U32, STR, U32, U64,
    ARRAY, U64, U64, U64, U64, U64, U64, U64, U8, U64, U8, U64, ARRAY, ARRAY, STR, U32, U16, U8,
};
static_assert(sizeof(kFields121) / sizeof(kFields121[0]) == 78, "GET_PLAYER has 78 fields");

size_t field_size(Kind k) {
  switch (k) {
    case U8: return 1;
    case U16: return 2;
    case U32: case F32: return 4;
    case U64: return 8;
    case GUID: return 16;
    case U8X30: return 30;
    default: return 0;
  }
}

bool split_fields(const std::vector<Kind>& kinds, const Bytes& body, std::vector<Bytes>* out, Raised* r) {
  out->clear();
  size_t pos = 0;
  for (Kind k : kinds) {
    size_t n = field_size(k);
    if (!n) {
      if (!check(body, pos, 4, r)) return false;
      n = 4 + static_cast<size_t>(u32(body, pos));
    }
    out->push_back(slice(body, pos, pos + n));
    pos += n;
  }
  if (pos != body.size()) {
    *r = Raised{"ValueError", "reply has " + std::to_string(body.size()) + " bytes, its fields " + std::to_string(pos)};
    return false;
  }
  return true;
}

enum OwnField { kTaken, kGarage, kParts, kAbilities };
struct PlayerField {
  Kind kind;
  int src;
  OwnField own;
};
const PlayerField kPlayerFields[] = {
    {U8, 0, kTaken},     {U64, 1, kTaken},       {U64, 2, kTaken},    {U64, 3, kTaken},    {GUID, 4, kTaken},
    {STR, 5, kTaken},    {U8, 7, kTaken},        {U32, 8, kTaken},    {U32, 9, kTaken},    {U32, 10, kTaken},
    {U32, 12, kTaken},   {U16, 13, kTaken},      {U64, 14, kTaken},   {U32, 15, kTaken},   {U64, 16, kTaken},
    {U64, 17, kTaken},   {F32, 18, kTaken},      {F32, 19, kTaken},   {F32, 20, kTaken},   {F32, 21, kTaken},
    {F32, 22, kTaken},   {F32, 23, kTaken},      {ARRAY, 24, kTaken}, {ARRAY, 25, kTaken}, {ARRAY, 26, kAbilities},
    {ARRAY, 27, kTaken}, {ARRAY, 28, kTaken},    {ARRAY, 29, kTaken}, {ARRAY, 30, kTaken}, {U32, 31, kTaken},
    {U8, 32, kTaken},    {U8X30, 33, kTaken},    {U32, 34, kTaken},   {U32, 35, kTaken},   {U32, 36, kTaken},
    {ARRAY, 37, kTaken}, {U64, 71, kTaken},      {ARRAY, -1, kGarage}, {ARRAY, -1, kParts}, {U32, 75, kTaken},
    {U8, 77, kTaken},
};

std::optional<Bytes> abilities_array(obj::Py& py, const V& words) {
  const uint64_t mask[2] = {~uint64_t(1), 0x1F};
  std::vector<V> head;
  if (words.is_array()) {
    for (size_t k = 0; k < std::min<size_t>(2, words.items().size()); k++) head.push_back(words.items()[k]);
  } else if (words.is_string()) {
    std::vector<V> chars = py.iter(words);
    for (size_t k = 0; k < std::min<size_t>(2, chars.size()); k++) head.push_back(chars[k]);
  } else if (words.is_object()) {
    py.raise("KeyError", "slice(None, 2, None)");
    return std::nullopt;
  } else {
    py.raise("TypeError", std::string("'") + obj::type_name(words) + "' object is not subscriptable");
    return std::nullopt;
  }
  V out = V::array();
  for (size_t k = 0; k < head.size(); k++) out.push(py.band(head[k], obj::U(mask[k])));
  if (py.failed()) return std::nullopt;
  while (!out.items().empty() && !obj::truthy(out.items().back())) out.items().pop_back();
  return pack::u64_array(py, out);
}

bool get_or_create_body(Ctx& ctx, const Bytes& body, Bytes* out, HandleResult* stop) {
  Bytes inner;
  if (!base_reply(ctx, 0x589F57FB, body, &inner, stop)) return false;
  Raised r;
  std::vector<Bytes> fields;
  std::vector<Kind> kinds(std::begin(kFields121), std::end(kFields121));
  if (!split_fields(kinds, slice(inner, 12, inner.size()), &fields, &r)) {
    *stop = fail(r);
    return false;
  }
  const cars121::Catalog* catalog = cars121::catalog(*ctx.core);
  std::optional<cars121::Garage> garage = cars121::entries(ctx.player, &r);
  if (!garage) {
    *stop = fail(r);
    return false;
  }
  Bytes garage_field = wire::empty_array();
  if (!garage->empty() && !cars::garage_field(*garage, catalog, &garage_field, &r)) {
    *stop = fail(r);
    return false;
  }
  Bytes parts_field;
  if (!cars::parts_field(*garage, ctx.player.owned_items, catalog, &parts_field, &r)) {
    *stop = fail(r);
    return false;
  }
  Bytes abilities_field = wire::empty_array();
  if (obj::truthy(ctx.player.abilities)) {
    obj::Py py;
    std::optional<Bytes> a = abilities_array(py, ctx.player.abilities);
    if (!a) {
      *stop = fail(py.raised());
      return false;
    }
    abilities_field = std::move(*a);
  }
  out->clear();
  for (const PlayerField& f : kPlayerFields) {
    switch (f.own) {
      case kGarage: append(*out, garage_field); break;
      case kParts: append(*out, parts_field); break;
      case kAbilities: append(*out, abilities_field); break;
      default: append(*out, fields[static_cast<size_t>(f.src)]);
    }
  }
  return true;
}

HandleResult get_or_create_player(Ctx& ctx, const Bytes& body) {
  Bytes out;
  HandleResult stop;
  if (!get_or_create_body(ctx, body, &out, &stop)) return stop;
  return reply(0xA555E30A, out);
}

constexpr uint32_t T_RULE_PROXY = 0xD4150057;
constexpr uint32_t T_RULE_MODULO = 0xEDAC67C4;

bool rules_with_proxy(const Bytes& inner, Bytes* out, Raised* r) {
  Bytes body = slice(inner, 12, inner.size());
  if (!check(body, 0, 4, r)) return false;
  size_t n = u32(body, 0);
  Bytes lst = slice(body, 4, 4 + n);
  std::vector<Bytes> elems;
  size_t o = 12;
  while (o < lst.size()) {
    if (!check(lst, o, 4, r)) return false;
    size_t m = u32(lst, o);
    Bytes el = slice(lst, o + 4, o + 4 + m);
    o += 4 + m;
    if (!check(el, 12, 4, r)) return false;
    size_t sl = u32(el, 12);
    Bytes name = slice(el, 16, 16 + sl);
    if (!check(el, 4, 4, r)) return false;
    static const std::string kRecords = "RecordsService";
    if (u32(el, 4) == T_RULE_MODULO && std::string(name.begin(), name.end()) == kRecords) {
      el = wire::frame(T_RULE_PROXY, wire::blob(name));
    }
    elems.push_back(std::move(el));
  }
  if (!check(inner, 4, 4, r)) return false;
  Bytes rest = wire::blob(wire::serializer_list(elems));
  append(rest, slice(body, 4 + n, body.size()));
  *out = wire::frame(u32(inner, 4), rest);
  return true;
}

HandleResult scalability_rules(Ctx& ctx, const Bytes& body) {
  Bytes inner;
  HandleResult stop;
  if (!base_reply(ctx, 0x27C711C5, body, &inner, &stop)) return stop;
  Bytes rules;
  Raised r;
  if (!rules_with_proxy(inner, &rules, &r)) return fail(r);
  return reply(u32(rules, 4), slice(rules, 12, rules.size()));
}

HandleResult get_pvp_score(Ctx&, const Bytes&) {
  Bytes out{1};
  append(out, wire::blob(wire::serializer_list({})));
  return reply(0x2B1D5CA7, out);
}

bool totals(const Player& p, Bytes& out, Raised* r) {
  pack::Packer pk;
  pk.I(p.level).I(p.xp).I(p.bucks).I(p.cc()).I(p.perk_points);
  if (pk.failed()) {
    *r = pk.raised();
    return false;
  }
  append(out, pk.out());
  return true;
}

HandleResult validate_mission(Ctx& ctx, const Bytes& body) {
  Raised r;
  if (!check(body, 0x1C, 12, &r)) return fail(r);
  uint64_t mission = u64(body, 0x1C);
  int64_t score = u32(body, 0x24);
  Player& p = ctx.player;
  std::optional<bool> dealer = v1_21::pays_dealer(ctx, p, mission, &r);
  if (!dealer) return fail(r);
  if (!v1_21::finish_mission(*ctx.core, p, mission, score, ctx.now_us(), true, &r)) return fail(r);
  std::optional<int64_t> part;
  if (!v1_21::mission_reward_part(ctx, p, mission, score, body, &part, &r)) return fail(r);
  std::optional<Bytes> car;
  if (*dealer) {
    if (!v1_21::dealer_car_part(ctx, p, body, &car, &r)) return fail(r);
  } else if (v1_21::dealer_mission(ctx, mission)) {
    if (!v1_21::owned_dealer_car_part(ctx, p, body, &car, &r)) return fail(r);
  }
  if (std::optional<Raised> f = p.save()) return fail(*f);
  Bytes granted = wire::empty_array();
  if (part && v1_21::physparts::ANNOUNCE_PART && *part < cars::perf_parts()) {
    const v1_21::physparts::Table* t = v1_21::physparts::table();
    if (!t || static_cast<size_t>(*part) >= t->ids.size()) return fail(Raised{"IndexError", "list index out of range"});
    granted = wire::u64_array({t->ids[static_cast<size_t>(*part)]});
  }
  Bytes out{1};
  wire::put_u64(out, mission);
  if (!totals(p, out, &r)) return fail(r);
  append(out, granted);
  out.resize(out.size() + 12 * 4, 0);
  {
    obj::Py py;
    std::optional<Bytes> a = abilities_array(py, p.abilities);
    if (!a) return fail(py.raised());
    append(out, *a);
  }
  if (car && !car->empty()) {
    Bytes c;
    if (!cars::car_reply_from_121(*car, cars121::catalog(*ctx.core), &c, &r)) return fail(r);
    append(out, c);
  } else {
    append(out, cars::no_car());
  }
  wire::put_u32(out, 0);
  return reply(0xAED45904, out);
}

HandleResult progress_bit(Ctx& ctx, const Bytes& body) {
  Raised r;
  if (!check(body, 0x1C, 2, &r)) return fail(r);
  obj::Py py;
  ctx.player.set_progress_bit(py, static_cast<uint64_t>(body[0x1C]) << 8 | body[0x1D]);
  if (py.failed()) return fail(py.raised());
  if (std::optional<Raised> f = ctx.player.save()) return fail(*f);
  std::optional<Bytes> words = pack::u64_array(py, ctx.player.progress);
  if (!words) return fail(py.raised());
  Bytes out{1};
  append(out, *words);
  wire::put_u32(out, 0);
  return reply(0x51495E7F, out);
}

HandleResult retrieve_secret_car(Ctx& ctx, const Bytes& body) {
  Bytes inner;
  HandleResult stop;
  if (!base_reply(ctx, 0x88648CE9, body, &inner, &stop)) return stop;
  Raised r;
  if (!check(inner, 12, 10, &r)) return fail(r);
  uint16_t status = static_cast<uint16_t>(inner[12] << 8 | inner[13]);
  Bytes out;
  if (u64(inner, 14) == 0xFFFFFFFFFFFFFFFFull) {
    wire::put_u16(out, 0);
    append(out, cars::no_car());
    return reply(0x6939A37D, out);
  }
  wire::put_u16(out, status);
  Bytes car;
  if (!cars::car_reply_from_121(slice(inner, 14, inner.size()), cars121::catalog(*ctx.core), &car, &r)) return fail(r);
  append(out, car);
  return reply(0x6939A37D, out);
}

bool purchase_head(const Bytes& inner, Bytes* out, Raised* r) {
  if (!check(inner, 12, 14, r)) return false;
  wire::put_u32(*out, static_cast<uint32_t>(inner[12] << 8 | inner[13]));
  for (size_t o : {14, 18, 22}) wire::put_u32(*out, u32(inner, o));
  return true;
}

HandleResult buy_new_car(Ctx& ctx, const Bytes& body) {
  Bytes inner;
  HandleResult stop;
  if (!base_reply(ctx, 0xBBF8C44B, body, &inner, &stop)) return stop;
  Raised r;
  Bytes out;
  if (!purchase_head(inner, &out, &r)) return fail(r);
  Bytes car;
  if (!cars::car_reply_from_121(slice(inner, 26, inner.size()), cars121::catalog(*ctx.core), &car, &r)) return fail(r);
  append(out, car);
  return reply(0xA0B9C4D7, out);
}

HandleResult use_money(Ctx& ctx, const Bytes&) {
  Bytes out{1};
  Raised r;
  wire::put_u32(out, 0);
  pack::Packer pk;
  if (pk.I(ctx.player.bucks).failed()) return fail(pk.raised());
  append(out, pk.out());
  for (int i = 0; i < 4; i++) wire::put_u32(out, 0);
  return reply(auto_of(0xB9BE3AE1)->reply, out);
}

bool meta_block(const Ctx& ctx, Bytes& out, Raised* r) {
  const Player& p = ctx.player;
  wire::put_u64(out, ctx.player_id);
  pack::Packer pk;
  pk.B(p.group).Q(obj::truthy(p.group) ? p.group_joined : obj::I(0));
  if (pk.failed()) {
    *r = pk.raised();
    return false;
  }
  append(out, pk.out());
  out.resize(out.size() + 11 * 4, 0);
  if (!put(out, 'Q', ctx.now_us(), r)) return false;
  append(out, wire::empty_array());
  wire::put_u16(out, 0);
  wire::put_u32(out, 0);
  return true;
}

HandleResult meta_result(Ctx& ctx, uint32_t reply_id, size_t extra) {
  Bytes out{1};
  Raised r;
  if (!meta_block(ctx, out, &r) || !totals(ctx.player, out, &r)) return fail(r);
  out.resize(out.size() + 4 * extra, 0);
  return reply(reply_id, out);
}

HandleResult validate_faction_mission(Ctx& ctx, const Bytes&) { return meta_result(ctx, 0x1D31095E, 0); }
HandleResult collect_send_friend_mission(Ctx& ctx, const Bytes&) { return meta_result(ctx, 0xD9F5D2BF, 0); }
HandleResult validate_pvp_mission(Ctx& ctx, const Bytes&) { return meta_result(ctx, 0xDC5C75CA, 1); }

HandleResult sell_item(Ctx& ctx, const Bytes&) {
  Bytes out;
  Raised r;
  wire::put_u32(out, 0);
  pack::Packer pk;
  if (pk.I(ctx.player.bucks).failed()) return fail(pk.raised());
  append(out, pk.out());
  return reply(0x71F77321, out);
}

HandleResult save_crew_comforts(Ctx& ctx, const Bytes& body) {
  Bytes b = body;
  if (b.size() < 0x3D) b.resize(0x3D, 0);
  Bytes b121(0x3F, 0);
  std::copy(b.begin(), b.begin() + 0x1C, b121.begin());
  b121[0x1C] = b[0x1C];
  std::copy(b.begin() + 0x1F, b.begin() + 0x3D, b121.begin() + 0x21);
  Bytes inner;
  HandleResult stop;
  if (!base_reply(ctx, 0xBF9B8FBD, b121, &inner, &stop)) return stop;
  const Auto& a = *auto_of(0xEA9859E1);
  Bytes payload;
  Raised r;
  if (!translate::transcode(slice(inner, 12, inner.size()), {"u8", "u32*2", "u16*2"}, a.layout, nullptr, &payload,
                            &r)) {
    return fail(r);
  }
  return reply(a.reply, payload);
}

constexpr size_t DELETE_BACKUPS = 5;

Raised os_raised(const OsError& e) { return Raised{e.type(), e.text()}; }

std::string stem_of(Files& files, const std::string& path) {
  std::string dir = files.dirname(path);
  std::string name = path.size() >= dir.size() ? path.substr(dir.size()) : path;
  size_t lead = name.find_first_not_of("/\\");
  name = lead == std::string::npos ? std::string() : name.substr(lead);
  size_t dot = name.rfind('.');
  if (dot != std::string::npos && name.find_first_not_of('.') < dot) name = name.substr(0, dot);
  return name;
}

HandleResult delete_player(Ctx& ctx, const Bytes&) {
  Player& p = ctx.player;
  Files* files = p.files;
  if (!p.path.empty() && files && files->exists(p.path)) {
    OsError e;
    std::string abs;
    if (!files->abspath(p.path, &abs, &e)) return fail(os_raised(e));
    std::string backup = files->join(files->dirname(files->dirname(abs)), "backup");
    if (!files->makedirs(backup, &e)) return fail(os_raised(e));
    std::string stem = stem_of(*files, p.path);
    LocalTime t = ctx.core->env().now_local();
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", t.year, t.month, t.day, t.hour, t.minute,
                  t.second);
    if (!files->copyfile(p.path, files->join(backup, stem + "-deleted-" + stamp + ".json"), &e)) {
      return fail(os_raised(e));
    }
    std::vector<std::string> names;
    if (!files->listdir(backup, &names, &e)) return fail(os_raised(e));
    std::vector<std::string> own;
    const std::string prefix = stem + "-deleted-";
    for (const std::string& f : names) {
      if (f.compare(0, prefix.size(), prefix) == 0 && f.size() >= 5 && f.compare(f.size() - 5, 5, ".json") == 0) {
        own.push_back(f);
      }
    }
    std::sort(own.begin(), own.end(), [](const std::string& a, const std::string& b) { return py::fs_name_less(a, b); });
    for (size_t i = 0; i + DELETE_BACKUPS < own.size(); i++) {
      OsError ignored;
      files->remove(files->join(backup, own[i]), &ignored);
    }
  }
  Player fresh;
  fresh.path = p.path;
  fresh.files = p.files;
  fresh.max_bucks = p.max_bucks;
  fresh.unlocks = p.unlocks;
  fresh.dlc = p.dlc;
  fresh.free_roam = p.free_roam;
  p = std::move(fresh);
  Raised r;
  if (!ctx.core || !v1_21::start_crew_credits(*ctx.core, p, true, &r)) return fail(r);
  if (v1_21::truthy(p.free_roam)) p.unlock_free_roam();
  if (std::optional<Raised> f = p.save()) return fail(*f);
  Bytes out{1};
  wire::put_u32(out, 0);
  return reply(0x3EC53E54, out);
}

HandleResult create_player(Ctx& ctx, const Bytes& body) {
  Bytes inner;
  HandleResult stop;
  if (!get_or_create_body(ctx, body, &inner, &stop)) return stop;
  std::vector<Kind> kinds;
  for (const PlayerField& f : kPlayerFields) kinds.push_back(f.kind);
  std::vector<Bytes> fields;
  Raised r;
  if (!split_fields(kinds, inner, &fields, &r)) return fail(r);
  Bytes out = fields[0];
  wire::put_u32(out, 0);
  for (size_t i = 1; i + 2 < fields.size(); i++) append(out, fields[i]);
  return reply(0x86D9FE30, out);
}

HandleResult buy_dress(Ctx& ctx, const Bytes& body) {
  if (body.size() <= 0x1C) return fail(Raised{"IndexError", "index out of range"});
  uint8_t currency = body[0x1C];
  uint32_t count = 0, n = 0;
  uint64_t model = cars121::NONE;
  std::vector<uint64_t> ids;
  Raised ignored;
  bool ok = check(body, 0x1D + 16, 4, &ignored);
  if (ok) count = u32(body, 0x1D + 16);
  if (ok && count) {
    ok = check(body, 0x1D + 20, 8, &ignored);
    if (ok) model = u64(body, 0x1D + 20);
  }
  if (ok && count) {
    ok = check(body, 0x1D + 28 + 16, 4, &ignored);
    if (ok) n = u32(body, 0x1D + 28 + 16);
  }
  if (ok && n) {
    ok = check(body, 0x1D + 28 + 20, 7 * 8, &ignored);
    for (size_t k = 0; ok && k < 7; k++) ids.push_back(u64(body, 0x1D + 28 + 20 + 8 * k));
  }
  if (!ok) {
    currency = 0;
    count = 0;
    model = cars121::NONE;
    n = 0;
    ids.clear();
  }
  Bytes inner_array;
  if (n) {
    Bytes items;
    for (uint64_t id : ids) wire::put_u64(items, id);
    for (int k = 0; k < 3; k++) wire::put_u64(items, cars121::NONE);
    inner_array = cars121::array(1, items);
  } else {
    inner_array = cars121::array(0, Bytes());
  }
  Bytes b121 = slice(body, 0, 0x1C);
  b121.push_back(currency);
  if (count) {
    Bytes element;
    wire::put_u64(element, model);
    append(element, inner_array);
    append(b121, cars121::array(1, element));
  } else {
    append(b121, cars121::array(0, Bytes()));
  }
  Bytes inner;
  HandleResult stop;
  if (!base_reply(ctx, 0xF70D87D3, b121, &inner, &stop)) return stop;
  Raised r;
  Bytes out;
  if (!purchase_head(inner, &out, &r)) return fail(r);
  if (!check(inner, 26, 4, &r)) return fail(r);
  size_t blen = u32(inner, 26);
  Bytes car;
  if (!cars::car_reply_from_121(slice(inner, 26 + 4 + 16, 26 + 4 + blen), cars121::catalog(*ctx.core), &car, &r)) {
    return fail(r);
  }
  append(out, cars121::array(1, car));
  return reply(auto_of(0xF70D87D3)->reply, out);
}

struct OwnHandler {
  const char* name;
  Own fn;
};

const std::map<uint32_t, OwnHandler>& own_handlers() {
  static const std::map<uint32_t, OwnHandler> table = {
      {0x2E0E24CB, {"LOAD_ENTITY_READONLY", load_entity_read_only}},
      {0x5EAA9404, {"GET_OR_CREATE_PLAYER", get_or_create_player}},
      {0x27C711C5, {"SCALABILITY_RULES", scalability_rules}},
      {0x33F6A6A1, {"GET_PVP_SCORE", get_pvp_score}},
      {0xB84353AD, {"VALIDATE_MISSION", validate_mission}},
      {0xC43C7F34, {"PROGRESS_BIT", progress_bit}},
      {0x88648CE9, {"SECRET_CAR", retrieve_secret_car}},
      {0xB11F578E, {"BUY_NEW_CAR", buy_new_car}},
      {0xB9BE3AE1, {"USE_MONEY", use_money}},
      {0x31414F5F, {"FACTION_MISSION", validate_faction_mission}},
      {0x60875720, {"COLLECT_SEND_FRIEND_MISSION", collect_send_friend_mission}},
      {0xF99ADFB6, {"PVP_MISSION", validate_pvp_mission}},
      {0x0D0AE2E9, {"SELL_ITEM", sell_item}},
      {0xEA9859E1, {"PERKS", save_crew_comforts}},
      {0x19B432B3, {"DELETE_PLAYER", delete_player}},
      {0xABBB99E1, {"CREATE_PLAYER", create_player}},
      {0xF70D87D3, {"SPEC_PURCHASE", buy_dress}},
  };
  return table;
}

}

std::string name_of(uint32_t id) {
  auto own = own_handlers().find(id);
  if (own != own_handlers().end()) return own->second.name;
  if (const Auto* a = auto_of(id)) return a->name;
  return v1_21::name_of(id);
}

HandleResult handle(Ctx& ctx, uint32_t id, const Bytes& body) {
  auto own = own_handlers().find(id);
  if (own != own_handlers().end()) {
    if (std::optional<Raised> odd = ctx.player.clamp()) {
      HandleResult res;
      res.kind = HandleResult::kRaised;
      res.raised = *odd;
      return res;
    }
    return own->second.fn(ctx, body);
  }
  if (std::optional<uint32_t> req121 = answered_by_121(id)) return translated(ctx, id, *req121, body);
  if (const Auto* a = auto_of(id)) return auto_reply(*a);
  return v1_21::handle(ctx, id, body);
}

std::vector<uint32_t> probe_ids() {
  std::set<uint32_t> ids;
  for (const auto& h : own_handlers()) ids.insert(h.first);
  for (const auto& a : autos()) ids.insert(a.first);
  for (const auto& a : kAliases) ids.insert(a.first);
  return std::vector<uint32_t>(ids.begin(), ids.end());
}

}
}
