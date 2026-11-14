#include <map>

#include "core/v1_21/cars.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/summit.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

using pack::Packer;

Bytes text_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes content_file(uint64_t revision) {
  Bytes out;
  wire::put_u64(out, revision);
  Bytes empty;
  Bytes b = wire::blob(empty);
  out.insert(out.end(), b.begin(), b.end());
  wire::put_u32(out, 0);
  return out;
}

bool archetype_files(const Bytes& body, std::vector<std::pair<Bytes, uint64_t>>* files) {
  Raised ignored;
  uint32_t count = 0;
  if (!pack::unpack_u32(body, 16, &count, &ignored)) return false;
  size_t o = 20;
  for (uint32_t k = 0; k < count; k++) {
    uint32_t n = 0;
    if (!pack::unpack_u32(body, o, &n, &ignored)) return false;
    if (!pack::unpack_check(body, o + 4 + n, 8, &ignored)) return false;
    size_t a = std::min(o + 4, body.size()), b = std::min(o + 4 + n, body.size());
    files->emplace_back(Bytes(body.begin() + a, body.begin() + std::max(a, b)), wire::get_u64(body.data() + o + 4 + n));
    o += 12 + n;
  }
  return true;
}

HandlerResult archetypes(Ctx&, const Bytes& body, const Registered&) {
  std::vector<std::pair<Bytes, uint64_t>> files;
  if (!archetype_files(body, &files)) files = {{Bytes(), 0}, {Bytes(), 0}};
  Bytes array;
  wire::put_u32(array, static_cast<uint32_t>(files.size()));
  for (const auto& f : files) {
    Bytes name = wire::blob(f.first);
    array.insert(array.end(), name.begin(), name.end());
    array.push_back(0);
    Bytes c = content_file(f.second);
    array.insert(array.end(), c.begin(), c.end());
  }
  Bytes out{1};
  Bytes b = wire::blob(wire::frame(wire::T_ARRAY, array));
  out.insert(out.end(), b.begin(), b.end());
  return HandlerResult::reply(0x44A84B08, std::move(out));
}

HandlerResult login_queue(Ctx&, const Bytes&, const Registered&) {
  Bytes out;
  wire::put_u32(out, 0);
  wire::put_u64(out, 0);
  return HandlerResult::reply(0x9152C10D, std::move(out));
}

constexpr uint32_t T_CONFIG_SECTION = 0x5999B98B;
constexpr uint32_t T_CONFIG_VALUE = 0xBA2D37DC;

HandlerResult net_config(Ctx&, const Bytes&, const Registered&) {
  Bytes value = wire::blob(text_bytes("ALLOW_PLAY_WITHOUT_PUNCH"));
  Bytes v = wire::blob(text_bytes("1"));
  value.insert(value.end(), v.begin(), v.end());
  Bytes section = wire::blob(text_bytes("Punch"));
  Bytes values = wire::blob(wire::serializer_list({wire::frame(T_CONFIG_VALUE, value)}));
  section.insert(section.end(), values.begin(), values.end());
  Bytes out{1};
  Bytes list = wire::blob(wire::serializer_list({wire::frame(T_CONFIG_SECTION, section)}));
  out.insert(out.end(), list.begin(), list.end());
  return HandlerResult::reply(0x8A35ECD9, std::move(out));
}

HandlerResult probing_time(Ctx& ctx, const Bytes& body, const Registered&) {
  Bytes out(body.begin(), body.begin() + std::min<size_t>(8, body.size()));
  Raised raised;
  std::optional<int64_t> now = ctx.client_now_us(&raised);
  if (!now || !pack::put(out, 'Q', pack::Num::of_signed(*now), &raised)) return HandlerResult::fail(raised);
  return HandlerResult::reply(0x8386FE97, std::move(out));
}

enum Kind { U8, U16, U32, U64, F32, GUID, STR, ARRAY, U8X30 };
const Kind kFields[] = {
    U8,
    U64, U64, U64,
    GUID,
    STR,
    U8, U8, U32, U32, U32,
    ARRAY,
    U32, U16, U64, U32, U64, U64,
    F32, F32, F32, F32, F32, F32,
    ARRAY, ARRAY, ARRAY, ARRAY, ARRAY, ARRAY, ARRAY,
    U32, U8, U8X30, U32, U32, U32,
    ARRAY,
    U8, U16, U8, U8, U8, U8,
    U64, U64, U8, U32, U64, U64, U8, U8, U8, U64, ARRAY, U8, U32,
    STR, U32, U64,
    ARRAY,
    U64, U64, U64, U64, U64, U64, U64, U8, U64, U8,
    U64,
    ARRAY, ARRAY,
    STR, U32, U16, U8,
};
constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);
static_assert(kFieldCount == 78, "GET_PLAYER has 78 fields");

const char kStartDate[] = "20150101-120000";
constexpr size_t GETPLAYER_SLOTS = 250;

struct Val {
  bool is_bytes = false;
  Bytes bytes;
  V v = obj::I(0);

  static Val of(const V& x) {
    Val r;
    r.v = x;
    return r;
  }
  static Val of(Bytes b) {
    Val r;
    r.is_bytes = true;
    r.bytes = std::move(b);
    return r;
  }
  static Val u(uint64_t x) { return of(obj::U(x)); }
  bool truthy() const { return is_bytes ? !bytes.empty() : obj::truthy(v); }
  const char* type() const { return is_bytes ? "bytes" : obj::type_name(v); }
};

std::optional<Bytes> bytes_of(obj::Py& py, const V& x) {
  if (x.is_string()) {
    py.raise("TypeError", "string argument without an encoding");
    return std::nullopt;
  }
  if (x.is_int() || x.is_bool()) {
    std::optional<int64_t> n = obj::small_int(x);
    if (!n || *n < 0) {
      py.raise(n ? "ValueError" : "OverflowError", n ? "negative count" : "cannot fit 'int' into an index-sized integer");
      return std::nullopt;
    }
    return Bytes(static_cast<size_t>(*n), 0);
  }
  if (!x.is_array() && !x.is_object()) {
    py.raise("TypeError", std::string("cannot convert '") + obj::type_name(x) + "' object to bytes");
    return std::nullopt;
  }
  Bytes out;
  for (const V& item : py.iter(x)) {
    if (!item.is_int() && !item.is_bool()) {
      py.raise("TypeError", std::string("'") + obj::type_name(item) + "' object cannot be interpreted as an integer");
      return std::nullopt;
    }
    std::optional<int64_t> b = obj::small_int(item);
    if (!b || *b < 0 || *b > 255) {
      py.raise("ValueError", "bytes must be in range(0, 256)");
      return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>(*b));
  }
  return out;
}

HandlerResult get_player(Ctx& ctx, const Bytes&, const Registered&) {
  std::map<int64_t, Val> values;
  for (int k : {0, 6, 7, 38, 55, 77}) values[k] = Val::u(1);
  for (int k : {3, 14, 16, 17, 44, 45, 48, 49, 61, 62, 63, 64, 65, 66, 67}) values[k] = Val::u(NO_ID);
  values[43] = Val::u(3);
  values[52] = Val::u(3);
  values[50] = Val::u(0xFF);
  values[51] = Val::u(0xFF);
  values[69] = Val::u(0xFF);
  values[57] = Val::of(Bytes(kStartDate, kStartDate + sizeof(kStartDate) - 1));
  values[74] = Val::of(Bytes(kStartDate, kStartDate + sizeof(kStartDate) - 1));
  Player& p = ctx.player;
  obj::Py py;
  auto failed = [&]() { return HandlerResult::fail(py.raised()); };
  p.update_charge = obj::I(0);
  V id0 = py.getitem(p.ids, obj::I(0));
  V id1 = py.getitem(p.ids, obj::I(1));
  std::vector<V> place = py.iter(py.add(p.position, p.direction));
  if (py.failed()) return failed();
  values[7] = Val::of(obj::I(p.level));
  values[8] = Val::of(p.xp);
  values[9] = Val::of(p.bucks);
  values[10] = Val::of(obj::I(p.cc()));
  values[12] = Val::of(obj::I(p.field12));
  values[13] = Val::of(obj::I(p.perk_points));
  values[15] = Val::of(p.zone);
  values[16] = Val::of(id0);
  values[17] = Val::of(id1);
  for (size_t i = 0; i < place.size(); i++) values[18 + static_cast<int64_t>(i)] = Val::of(place[i]);
  auto bit_set = [&](const V& words, int64_t field) {
    if (!obj::truthy(words)) return;
    std::optional<Bytes> b = pack::u64_array(py, words);
    if (b) values[field] = Val::of(std::move(*b));
  };
  bit_set(p.progress, 30);
  bit_set(p.missions_done, 25);
  bit_set(p.map_state, 27);
  if (py.failed()) return failed();
  if (obj::truthy(p.progress) || obj::truthy(p.map_state) || obj::truthy(p.missions_done)) values[77] = Val::u(0);
  bit_set(p.abilities, 26);
  bit_set(p.rewarded, 28);
  bit_set(p.table_bits, 29);
  std::optional<Bytes> perks = bytes_of(py, p.perks);
  if (!perks) return failed();
  values[33] = Val::of(std::move(*perks));
  Raised when;
  std::optional<int64_t> cur = summit::current(ctx.now_us(), gamedata::summit(), &when);
  if (!cur) return HandlerResult::fail(when);
  summit::sync(py, p, *cur);
  V* s = summit::state(py, p);
  if (!s) return failed();
  const V& tokens = *s->get("tokens");
  values[38] = Val::of(*s->get("threshold"));
  values[39] = Val::of(*s->get("last_kind"));
  for (int i = 0; i < 3; i++) values[40 + i] = Val::of(py.getitem(tokens, obj::I(i)));
  if (py.failed()) return failed();
  values[43] = Val::of(*s->get("next_kind"));
  values[55] = Val::u(obj::truthy(*s->get("entered")) ? 0 : 1);
  {
    std::vector<V> ids = py.iter(p.save_ids);
    if (py.failed()) return failed();
    for (size_t i = 0; i < ids.size(); i++) values[61 + static_cast<int64_t>(i)] = Val::of(ids[i]);
  }
  std::vector<V> chase;
  if (!py.unpack(p.chase, 2, &chase)) return failed();
  values[67] = Val::of(chase[0]);
  values[69] = Val::of(chase[1]);
  values[1] = Val::u(ctx.player_id);
  values[5] = Val::of(Bytes(ctx.player_name.begin(), ctx.player_name.end()));
  values[71] = Val::u(ctx.player_id);
  Bytes slots;
  wire::put_u32(slots, GETPLAYER_SLOTS);
  slots.resize(slots.size() + 8 * GETPLAYER_SLOTS, 0);
  values[73] = Val::of(wire::blob(wire::frame(wire::T_ARRAY, slots)));
  for (const auto& d : p.dlc) values[d.first] = Val::of(d.second);
  Raised raised;
  std::optional<cars::Garage> garage = cars::entries(p, &raised);
  if (!garage) return HandlerResult::fail(raised);
  if (!garage->empty()) {
    std::optional<Bytes> field = cars::garage_field(*garage, &raised);
    if (!field) return HandlerResult::fail(raised);
    values[72] = Val::of(std::move(*field));
  } else {
    values.erase(72);
  }
  std::vector<std::pair<int64_t, Bytes>> extra;
  for (const auto& e : p.extra_fields) {
    std::optional<Bytes> b = py.fromhex(e.second);
    if (!b) return failed();
    std::optional<int64_t> k = py::int_text_to_i64(e.first);
    if (k) extra.emplace_back(*k, std::move(*b));
  }
  for (auto& e : extra) values[e.first] = Val::of(std::move(e.second));
  for (const auto& u : p.unlocks) values[u.first] = Val::of(u.second);
  size_t owned_n = py.len(p.owned_items);
  Bytes owned;
  for (const V& w : py.iter(p.owned_items)) {
    if (!py.pack(owned, 'Q', w)) break;
  }
  if (py.failed()) return failed();
  const Val& v73 = values[73];
  if (!v73.is_bytes) {
    return HandlerResult::fail(Raised{"TypeError", std::string("a bytes-like object is required, not '") + v73.type() +
                                                       "'"});
  }
  std::optional<Bytes> parts = cars::player_parts(*garage, {v73.bytes, cars::array(static_cast<uint32_t>(owned_n),
                                                                                   owned)}, &raised);
  if (!parts) return HandlerResult::fail(raised);
  values[73] = Val::of(std::move(*parts));
  Bytes out;
  for (size_t k = 0; k < kFieldCount; k++) {
    auto it = values.find(static_cast<int64_t>(k));
    Val value = it == values.end() ? Val::u(0) : it->second;
    Kind kind = kFields[k];
    switch (kind) {
      case U8:
      case U16:
      case U32:
      case U64:
        if (value.is_bytes) {
          out.insert(out.end(), value.bytes.begin(), value.bytes.end());
        } else if (!py.pack(out, kind == U8 ? 'B' : kind == U16 ? 'H' : kind == U32 ? 'I' : 'Q', value.v)) {
          return failed();
        }
        break;
      case F32:
        if (value.is_bytes) return HandlerResult::fail(pack::error("required argument is not an integer"));
        if (!py.pack(out, 'I', value.v)) return failed();
        break;
      case U8X30:
        if (value.is_bytes) {
          Bytes b(value.bytes.begin(), value.bytes.begin() + std::min<size_t>(30, value.bytes.size()));
          b.resize(30, 0);
          out.insert(out.end(), b.begin(), b.end());
        } else {
          out.resize(out.size() + 30, 0);
        }
        break;
      case GUID:
        out.resize(out.size() + 16, 0);
        break;
      case STR: {
        if (!value.is_bytes && value.truthy()) {
          const V& x = value.v;
          bool sized = x.is_string() || x.is_array() || x.is_object();
          return HandlerResult::fail(Raised{"TypeError", sized ? std::string("can't concat ") + value.type() + " to bytes"
                                                               : std::string("object of type '") + value.type() +
                                                                     "' has no len()"});
        }
        Bytes b = wire::blob(value.is_bytes ? value.bytes : Bytes());
        out.insert(out.end(), b.begin(), b.end());
        break;
      }
      case ARRAY: {
        if (!value.is_bytes && value.truthy()) {
          return HandlerResult::fail(Raised{"TypeError", std::string("can't concat ") + value.type() + " to bytes"});
        }
        Bytes b = value.truthy() ? value.bytes : wire::empty_array();
        out.insert(out.end(), b.begin(), b.end());
        break;
      }
    }
  }
  return HandlerResult::reply(0x37E9E238, std::move(out));
}

void add(std::vector<Registered>& out, uint32_t id, const char* name, Handler fn) {
  Registered r;
  r.id = id;
  r.name = name;
  r.fn = fn;
  out.push_back(std::move(r));
}

}

void register_login(std::vector<Registered>& out) {
  add(out, 0xCD3B60F2, "GAME_ARCHETYPES", archetypes);
  add(out, 0x036D7773, "LOGIN_QUEUE", login_queue);
  add(out, 0x9AA22640, "NET_CONFIG_VALUES", net_config);
  add(out, 0xB1F54E5C, "PROBING_TIME", probing_time);
  add(out, 0x589F57FB, "GET_PLAYER", get_player);
}

}
}
