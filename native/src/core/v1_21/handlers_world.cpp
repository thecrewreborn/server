#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "core/core.h"
#include "core/pymath.h"
#include "core/v1_21/factions.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/mission.h"
#include "core/v1_21/module_state.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/replies.h"
#include "core/v1_21/rewards.h"
#include "core/v1_21/summit.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

using obj::I;
using obj::Py;
using obj::U;
using pack::Packer;

bool pack_into(Py& py, Bytes& out, size_t offset, char code, const V& v) {
  Bytes one;
  if (!py.pack(one, code, v)) return false;
  if (offset + one.size() > out.size()) {
    return py.raise("struct.error", "pack_into requires a buffer of at least " + py::uint_to_text(offset + one.size()) +
                                        " bytes for packing " + py::uint_to_text(one.size()) + " bytes at offset " +
                                        py::uint_to_text(offset) + " (actual buffer size is " +
                                        py::uint_to_text(out.size()) + ")");
  }
  std::copy(one.begin(), one.end(), out.begin() + static_cast<std::ptrdiff_t>(offset));
  return true;
}

Bytes ljust(const Bytes& body, size_t n) {
  Bytes out = body;
  if (out.size() < n) out.resize(n, 0);
  return out;
}

uint32_t be32(const Bytes& b, size_t o) { return wire::get_u32(b.data() + o); }
uint64_t be64(const Bytes& b, size_t o) { return wire::get_u64(b.data() + o); }

HandlerResult saved_or(Ctx& ctx, HandlerResult reply) {
  if (std::optional<Raised> failed = ctx.player.save()) return HandlerResult::fail(*failed);
  return reply;
}

void add(std::vector<Registered>& out, uint32_t id, const char* name, Handler fn) {
  Registered r;
  r.id = id;
  r.name = name;
  r.fn = fn;
  out.push_back(std::move(r));
}

const uint64_t WINDOW_2017[2] = {0x554F34B42AC00ull, 0x560F920FB0C00ull};
const uint64_t WINDOW_2023[2] = {0x60AC9080C6C00ull, 0x60D248749AC00ull};

void head(Packer& pk, int64_t status, const Player& p) {
  pk.H(status).I(p.level).I(p.xp).I(p.bucks).H(p.perk_points).B(0x28).H(0).B(0).B(0).B(0).B(1);
}

void block(Packer& pk, int64_t now, int64_t tail, int64_t last) {
  pk.Qs(now).Q(NO_ID).B(0).I(0).Q(WINDOW_2023[0]).Q(WINDOW_2023[1]).B(3).B(0x14).B(tail).Qs(last);
  pk.raw(wire::empty_array());
}

std::optional<int64_t> cur_of(Ctx& ctx, Py& py) {
  Raised when;
  std::optional<int64_t> cur = summit::current(ctx.now_us(), gamedata::summit(), &when);
  if (!cur) {
    py.raise(when);
    return std::nullopt;
  }
  summit::sync(py, ctx.player, *cur);
  if (py.failed()) return std::nullopt;
  return cur;
}

HandlerResult reply_of(uint32_t type, Packer& pk) {
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  return HandlerResult::reply(type, std::move(pk.out()));
}

HandlerResult fail_of(const Py& py) { return HandlerResult::fail(py.raised()); }

HandlerResult summit_entry(Ctx& ctx, const Bytes&, const Registered&) {
  Player& p = ctx.player;
  Py py;
  std::optional<int64_t> cur = cur_of(ctx, py);
  if (!cur) return fail_of(py);
  bool entered = summit::enter(py, p, *cur);
  if (py.failed()) return fail_of(py);
  if (entered) {
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  Packer pk;
  pk.H(0).B(1).I(p.bucks).Qs(ctx.now_us()).Q(NO_ID).B(0).I(0).Q(WINDOW_2017[0]).Q(WINDOW_2017[1]).B(3).B(0x1E).B(3);
  pk.Q(uint64_t(0)).raw(wire::empty_array()).B(1).I(0);
  return reply_of(0x5BFA99B8, pk);
}

HandlerResult summit_event(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x50);
  uint64_t mission = be64(body, 0x1C);
  uint32_t score = be32(body, 0x25);
  Player& p = ctx.player;
  Py py;
  std::optional<int64_t> now_cur = cur_of(ctx, py);
  if (!now_cur) return fail_of(py);
  int64_t cur = *now_cur;
  std::optional<int64_t> slot = summit::event_slot(mission, cur, gamedata::summit());
  bool ok = false;
  if (slot) {
    V* s = summit::state(py, p);
    if (!s) return fail_of(py);
    ok = obj::truthy(*s->get("entered"));
  }
  if (ok) {
    std::pair<int64_t, int64_t> reward = summit::event_reward(cur, gamedata::summit());
    std::pair<V, V> got = rewards::allow(py, *ctx.core, p, ctx.now_us(), I(reward.first), I(reward.second));
    if (const rewards::Tables* t = rewards::tables(*ctx.core)) {
      rewards::apply(py, *t, p, got.first, got.second, ctx.core->module_random());
    }
    summit::post_score(py, p, *slot, score);
    if (py.failed()) return fail_of(py);
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  int64_t now = ctx.now_us();
  Packer pk;
  head(pk, ok ? 0 : 1, p);
  block(pk, now, 0, now);
  pk.B(0).I(0x113).B(1).I(0x1E48C);
  if (ok) {
    pk.raw(wire::u64_array({NO_ID})).I(0).I(0).I(0x65);
  } else {
    pk.raw(wire::empty_array()).I(0).I(0xFFFFFFFF).I(0x65);
  }
  return reply_of(0xCF266AB3, pk);
}

HandlerResult summit_result(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x50);
  uint32_t rank = be32(body, 0x1C);
  Player& p = ctx.player;
  Py py;
  std::optional<int64_t> now_cur = cur_of(ctx, py);
  if (!now_cur) return fail_of(py);
  int64_t cur = *now_cur;
  V* s = summit::state(py, p);
  if (!s) return fail_of(py);
  std::pair<V, V> got = rewards::allow(py, *ctx.core, p, ctx.now_us(), I(0), I(1));
  if (const rewards::Tables* t = rewards::tables(*ctx.core)) {
    rewards::add_bucks(py, *t, p, got.second);
  } else {
    p.bucks = py.add(p.bucks, got.second, true);
  }
  if (py.failed()) return fail_of(py);
  const V& tokens = *s->get("tokens");
  if (!tokens.is_array() && !tokens.is_object() && !tokens.is_string()) {
    return HandlerResult::fail(
        Raised{"TypeError", std::string("Value after * must be an iterable, not ") + obj::type_name(tokens)});
  }
  std::vector<V> items = py.iter(tokens);
  if (items.size() != 3) {
    return HandlerResult::fail(pack::error("pack expected 4 items for packing (got " +
                                           py::uint_to_text(items.size() + 1) + ")"));
  }
  Packer snapshot;
  for (const V& x : items) snapshot.B(x);
  snapshot.B(*s->get("next_kind"));
  if (snapshot.failed()) return HandlerResult::fail(snapshot.raised());
  V entered = *s->get("entered");
  summit::result(py, p, rank, cur, ctx.core->module_random(), gamedata::summit());
  if (py.failed()) return fail_of(py);
  if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  Packer pk;
  pk.H(0).I(p.bucks).B(0x28).H(0).raw(snapshot.out());
  block(pk, ctx.now_us(), 3, 0);
  pk.B(1).I(0x1CB).B(1).I(0x258).B(entered).B(0).I(0).I(0x35);
  return reply_of(0xCCF7FCFC, pk);
}

HandlerResult summit_status(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x50);
  Player& p = ctx.player;
  Py py;
  if (!cur_of(ctx, py)) return fail_of(py);
  V* s = summit::state(py, p);
  if (!s) return fail_of(py);
  s->set("value", I(be32(body, 0x1E)));
  V results = py.add(*s->get("results"), I(1), true);
  if (py.failed()) return fail_of(py);
  s->set("results", std::move(results));
  if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  int64_t now = ctx.now_us();
  Packer pk;
  head(pk, 0, p);
  block(pk, now, 0, now);
  s = summit::state(py, p);
  pk.B(1).I(0x31).B(1).I(0x2D7C8).I(0).B(*s->get("entered")).B(0).I(0).I(3).I(0x172C);
  pk.Q(ctx.player_id);
  return reply_of(0xE4EA417B, pk);
}

struct ScoreKey {
  bool fits = true;
  int64_t value = 0;
  std::string text;
};

bool key_less(const ScoreKey& a, const ScoreKey& b) {
  if (a.fits && b.fits) return a.value < b.value;
  bool na = a.text[0] == '-', nb = b.text[0] == '-';
  if (na != nb) return na;
  std::string ma = na ? a.text.substr(1) : a.text, mb = nb ? b.text.substr(1) : b.text;
  bool less_mag = ma.size() != mb.size() ? ma.size() < mb.size() : ma < mb;
  bool equal = ma == mb;
  if (equal) return false;
  return na ? !less_mag : less_mag;
}

HandlerResult summit_scores(Ctx& ctx, const Bytes&, const Registered&) {
  Player& p = ctx.player;
  Py py;
  if (!cur_of(ctx, py)) return fail_of(py);
  V* s = summit::state(py, p);
  if (!s) return fail_of(py);
  const V& scores = *s->get("scores");
  if (!scores.is_object()) {
    return HandlerResult::fail(
        Raised{"AttributeError", std::string("'") + obj::type_name(scores) + "' object has no attribute 'items'"});
  }
  std::vector<std::pair<ScoreKey, const V*>> entries;
  for (const json::Member& m : scores.members()) {
    std::string err;
    std::optional<std::string> text = m.int_key ? std::optional<std::string>(m.key) : py::parse_int(m.key, &err);
    if (!text) return HandlerResult::fail(Raised{"ValueError", err});
    ScoreKey k;
    k.text = *text;
    std::optional<int64_t> v = py::int_text_to_i64(*text);
    k.fits = v.has_value();
    k.value = v.value_or(0);
    entries.emplace_back(std::move(k), &m.value);
  }
  Py order;
  auto tuple_less = [&](const std::pair<ScoreKey, const V*>& a, const std::pair<ScoreKey, const V*>& b) {
    if (order.failed()) return false;
    if (key_less(a.first, b.first)) return true;
    if (key_less(b.first, a.first)) return false;
    if (obj::same(*a.second, *b.second)) return false;
    return order.lt(*a.second, *b.second);
  };
  py::list_sort(entries, tuple_less);
  if (order.failed()) return fail_of(order);
  Packer pk;
  if (entries.empty()) {
    pk.B(1).I(0xFFFFFFFF).I(0).raw(wire::empty_array()).B(1);
    return reply_of(0x262AF119, pk);
  }
  Packer list;
  list.I(static_cast<int64_t>(entries.size()));
  for (const auto& e : entries) list.B(V::integer_text(e.first.text)).Q(*e.second).Q(uint64_t(1));
  if (list.failed()) return HandlerResult::fail(list.raised());
  pk.B(4).I(0).I(1).raw(wire::blob(wire::frame(wire::T_ARRAY, list.out()))).B(1);
  return reply_of(0x262AF119, pk);
}

HandlerResult save_ids(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x50);
  std::vector<uint64_t> ids;
  for (size_t k = 0; k < 6; k++) ids.push_back(be64(body, 0x1C + 8 * k));
  ctx.player.save_ids = obj::list_of_u64(ids);
  return saved_or(ctx, HandlerResult::reply(0x829BB4D2, Bytes{0x01, 0x00, 0x00}));
}

HandlerResult set_group(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x24);
  ctx.crew_group = be64(body, 0x1C);
  return HandlerResult::reply(0x3069959F, Bytes{0x01, 0x00, 0x00});
}

HandlerResult register_group(Ctx& ctx, const Bytes&, const Registered&) {
  uint64_t gid = ctx.core->v1_21_state().next_group_id++;
  Bytes out{0x01};
  wire::put_u64(out, gid);
  return HandlerResult::reply(0x10B92C81, std::move(out));
}

HandlerResult keep_alive(Ctx&, const Bytes&, const Registered&) { return HandlerResult::reply(0xA3EFE71A, Bytes{1}); }

HandlerResult leave(Ctx&, const Bytes&, const Registered&) { return HandlerResult::reply(0xFAFBF944, Bytes{1}); }

constexpr int64_t DAY = 86400;
constexpr int64_t DAILY_BUCKS = 2040;

struct Slot {
  size_t offset;
  char code;
  size_t group;
  int field;
};

std::vector<Slot> slots_of(const Bytes& body) {
  std::vector<Slot> out;
  std::vector<bool> taken(body.size() + 8, false);
  const int tiers[] = {0x54, 0x58, 0x5C, 0x60, 0x64};
  for (size_t o = 0; o + 8 <= body.size(); o++) {
    int64_t v = static_cast<int64_t>(wire::get_u64(body.data() + o));
    for (size_t g = 0; g < factions::GROUPS; g++) {
      for (int off : tiers) {
        if (v == factions::start_field(g, off) && !taken[o]) {
          out.push_back(Slot{o, 'Q', g, off});
          for (size_t k = o; k < o + 8; k++) taken[k] = true;
        }
      }
    }
  }
  for (size_t o = 0; o + 4 <= body.size(); o++) {
    if (taken[o]) continue;
    int64_t v = wire::get_u32(body.data() + o);
    for (size_t g = 0; g < factions::GROUPS; g++) {
      if (v == factions::start_field(g, 0x68)) out.push_back(Slot{o, 'I', g, 0x68});
    }
  }
  return out;
}

const std::vector<Slot>& slots(uint32_t req) {
  static const std::map<uint32_t, std::vector<Slot>> found = [] {
    std::map<uint32_t, std::vector<Slot>> m;
    for (uint32_t r : {0x594BCC53u, 0x82B2A771u, 0xA7476B7Du}) {
      const gamedata::ReplyTemplate* t = gamedata::reply_template(r);
      m.emplace(r, t ? slots_of(t->body) : std::vector<Slot>());
    }
    return m;
  }();
  static const std::vector<Slot> none;
  auto it = found.find(req);
  return it == found.end() ? none : it->second;
}

size_t own_block(uint32_t req) { return req == 0xA7476B7D ? 26 : req == 0x594BCC53 ? 54 : 53; }

HandlerResult status(Ctx& ctx, uint32_t req, size_t group_at, size_t joined_at, std::optional<size_t> unset_at) {
  const gamedata::ReplyTemplate* t = gamedata::reply_template(req);
  if (!t) {
    char key[16];
    std::snprintf(key, sizeof(key), "0x%08x", req);
    return HandlerResult::fail(Raised{"KeyError", std::string("'") + key + "'"});
  }
  Player& p = ctx.player;
  Raised raised;
  std::optional<Bytes> filled = replies::fill(ctx, t->body, t->fields, &raised);
  if (!filled) return HandlerResult::fail(raised);
  Bytes out = std::move(*filled);
  std::optional<factions::Groups> groups = factions::load(*ctx.core, py::floor_div(ctx.now_us(), 1000000), &raised);
  if (!groups) return HandlerResult::fail(raised);
  Py py;
  for (const Slot& s : slots(req)) {
    V v = factions::field(py, py.getitem(*groups, U(s.group)), s.field);
    if (!pack_into(py, out, s.offset, s.code, v)) return fail_of(py);
  }
  bool own = py.le(I(1), p.group) && py.le(p.group, U(py.len(*groups)));
  if (py.failed()) return fail_of(py);
  if (own) {
    V rec = py.getitem(*groups, py.sub(p.group, I(1)));
    std::vector<V> fields;
    for (int off = 0x6C; off < 0x9C; off += 4) fields.push_back(factions::field(py, rec, off));
    if (py.failed()) return fail_of(py);
    Packer pk;
    for (const V& v : fields) pk.I(v);
    if (pk.failed()) return HandlerResult::fail(pk.raised());
    if (own_block(req) + pk.out().size() > out.size()) {
      size_t at = own_block(req), n = pk.out().size();
      return HandlerResult::fail(pack::error("pack_into requires a buffer of at least " + std::to_string(at + n) +
                                             " bytes for packing " + std::to_string(n) + " bytes at offset " +
                                             std::to_string(at) + " (actual buffer size is " +
                                             std::to_string(out.size()) + ")"));
    }
    std::copy(pk.out().begin(), pk.out().end(), out.begin() + static_cast<std::ptrdiff_t>(own_block(req)));
  }
  if (!p.group.is_int() && !p.group.is_bool()) {
    return HandlerResult::fail(
        Raised{"TypeError", std::string("'") + obj::type_name(p.group) + "' object cannot be interpreted as an integer"});
  }
  std::optional<int64_t> g = obj::small_int(p.group);
  if (!g || *g < 0 || *g > 255) return HandlerResult::fail(Raised{"ValueError", "byte must be in range(0, 256)"});
  out[group_at] = static_cast<uint8_t>(*g);
  const bool grouped = obj::truthy(p.group);
  if (!pack_into(py, out, joined_at, 'Q', grouped ? p.group_joined : I(0))) return fail_of(py);
  if (unset_at && !pack_into(py, out, *unset_at, 'I', I(grouped ? 0 : 0xFFFFFFFF))) return fail_of(py);
  return HandlerResult::reply(t->type, std::move(out));
}

HandlerResult group_join(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x1E);
  Player& p = ctx.player;
  int64_t now = ctx.now_us();
  bool ok = !obj::eq(I(body[0x1C]), p.group);
  if (ok) {
    p.group = I(body[0x1C]);
    p.group_joined = I(now);
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  Packer pk;
  pk.B(ok ? 1 : 0).Q(ctx.player_id).B(p.group).Q(obj::truthy(p.group) ? p.group_joined : I(now)).Qs(now).I(0);
  pk.I(0xFFFFFFFF);
  return reply_of(0x9FD2A05A, pk);
}

HandlerResult daily_reward(Ctx& ctx, const Bytes&, const Registered&) {
  Player& p = ctx.player;
  const V now = I(py::floor_div(ctx.now_us(), 1000000));
  int64_t bucks = 0;
  Py py;
  bool due = obj::eq(p.daily_last, I(0)) || py.ge(py.sub(now, p.daily_last), I(DAY));
  if (py.failed()) return fail_of(py);
  if (due) {
    p.daily_last = now;
    bucks = obj::truthy(p.group) ? DAILY_BUCKS : 0;
    if (const rewards::Tables* t = rewards::tables(*ctx.core)) {
      rewards::add_bucks(py, *t, p, I(bucks));
    } else {
      p.bucks = py.add(p.bucks, I(bucks), true);
    }
    if (py.failed()) return fail_of(py);
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  HandlerResult r = status(ctx, 0xA7476B7D, 82, 91, size_t(103));
  if (r.kind != HandlerResult::kReply) return r;
  if (!pack_into(py, r.body, 307, 'I', I(bucks))) return fail_of(py);
  return r;
}

HandlerResult group_status(Ctx& ctx, const Bytes&, const Registered&) {
  return status(ctx, 0x594BCC53, 9, 18, size_t(30));
}

HandlerResult group_status2(Ctx& ctx, const Bytes&, const Registered&) {
  return status(ctx, 0x82B2A771, 8, 17, size_t(29));
}

constexpr double HEAT_CAP = 100000.0;
constexpr int64_t WANTED_CAP = 100000;
const uint64_t CHASE_CAR[2] = {0x2522F39, 0x794C74A};

V chase(Py& py, Ctx& ctx, bool escaped, int64_t wanted_in, uint64_t start, int64_t now) {
  Player& p = ctx.player;
  const V h = py.min(p.heat, obj::F(HEAT_CAP));
  const V wanted = I(std::min(wanted_in, WANTED_CAP));
  V xp = py.add(py.add(py.to_int(py.mul(wanted, obj::F(0.4))), I(100)), py.to_int(py.mul(obj::F(0.05), h)));
  V bonus = I(0), fine = I(0), bucks;
  if (escaped) {
    bonus = py.to_int(py.mul(obj::F(0.2), h));
    bucks = py.add(py.add(py.to_int(py.mul(wanted, obj::F(0.5))), I(500)), bonus);
    V grown = py.add(py.mul(U(static_cast<uint64_t>(now) - start), obj::F(0.5)), py.mul(wanted, obj::F(0.2)));
    V heat = py.max(py.to_float(py.to_int(py.add(grown, h))), obj::F(1.0));
    if (py.failed()) return V();
    p.heat = heat;
  } else {
    V cut = py.sub(py.sub(I(-500), py.to_int(py.mul(obj::F(0.2), h))), py.to_int(py.mul(wanted, obj::F(0.5))));
    fine = py.max(cut, I(-50000));
    bucks = fine;
    if (py.failed()) return V();
    p.heat = obj::F(1.0);
  }
  if (py.failed()) return V();
  std::pair<V, V> got = rewards::allow(py, *ctx.core, p, now * 1000000, xp, bucks);
  xp = got.first;
  bucks = got.second;
  if (escaped) bonus = py.min(bonus, bucks);
  if (const rewards::Tables* t = rewards::tables(*ctx.core)) {
    rewards::add_bucks(py, *t, p, bucks);
    rewards::add_xp(py, *t, p, xp, ctx.core->module_random());
  } else {
    V b = py.max(I(0), py.add(p.bucks, bucks));
    V x = py.add(p.xp, xp);
    if (py.failed()) return V();
    p.bucks = b;
    p.xp = x;
  }
  return py.band(py.add(bonus, fine), I(0xFFFFFFFF));
}

HandlerResult chase_reward(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x52);
  Player& p = ctx.player;
  uint64_t start = be64(body, 0x34);
  uint32_t wanted = be32(body, 0x3C);
  Py py;
  V value = chase(py, ctx, body[0x41] != 0, wanted, start, py::floor_div(ctx.now_us(), 1000000));
  if (py.failed()) return fail_of(py);
  bool none = obj::eq(py.getitem(p.chase, I(0)), U(NO_ID));
  if (py.failed()) return fail_of(py);
  if (none) p.chase = obj::list_of_u64({CHASE_CAR[0], CHASE_CAR[1]});
  if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  Bytes array;
  wire::put_u32(array, 1);
  Bytes r = rating(ctx.player_id);
  array.insert(array.end(), r.begin(), r.end());
  Bytes ratings = wire::blob(wire::frame(wire::T_ARRAY, array));
  Bytes ten;
  wire::put_u32(ten, 10);
  Packer pk;
  pk.H(0).I(p.level).I(p.xp).I(p.bucks).I(p.perk_points);
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  V car = py.getitem(p.chase, I(0));
  V next = py.getitem(p.chase, I(1));
  if (py.failed()) return fail_of(py);
  pk.Q(car).B(0).Q(next).B(0).Q(uint64_t(0)).raw(wire::empty_array());
  pk.Q(NO_ID).raw(wire::blob(wire::frame(wire::T_ARRAY, ten))).raw(wire::empty_array());
  pk.B(0).Q(NO_ID).B(0).I(0x8B3).I(0).I(300).I(value).I(0x68).raw(wire::empty_array());
  pk.I(0).raw(wire::empty_array()).raw(wire::empty_array()).raw(ratings).raw(ratings).H(0).B(0);
  return reply_of(0x2C81736C, pk);
}

}

void register_crew(std::vector<Registered>& out) {
  add(out, 0x9868D8CE, "PLAYER_SET_GROUP", set_group);
  add(out, 0x7FA3E7E6, "GROUP_REGISTER", register_group);
  add(out, 0x80A06106, "GROUP_KEEP_ALIVE", keep_alive);
  add(out, 0x33065129, "GROUP_LEAVE", leave);
}

void register_group(std::vector<Registered>& out) {
  add(out, 0x7932CE3E, "GROUP_JOIN", group_join);
  add(out, 0xA7476B7D, "DAILY_REWARD", daily_reward);
  add(out, 0x594BCC53, "GROUP_STATUS", group_status);
  add(out, 0x82B2A771, "GROUP_STATUS2", group_status2);
}

void register_police(std::vector<Registered>& out) {
  add(out, 0xF64F66EB, "CHASE_REWARD", chase_reward);
}

void register_summit(std::vector<Registered>& out) {
  add(out, 0x0BE43657, "SUMMIT_ENTRY", summit_entry);
  add(out, 0xB7E9BB59, "SUMMIT_EVENT", summit_event);
  add(out, 0xB9BA9057, "SUMMIT_RESULT", summit_result);
  add(out, 0x115C657D, "SUMMIT_STATUS", summit_status);
  add(out, 0xD14A2746, "SUMMIT_SCORES", summit_scores);
  add(out, 0xF282A24C, "SAVE_IDS", save_ids);
}

}
}
