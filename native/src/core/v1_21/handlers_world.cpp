#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "core/codec.h"
#include "core/core.h"
#include "core/pymath.h"
#include "core/v1_21/cars.h"
#include "core/v1_21/factions.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/handlers.h"
#include "core/v1_21/mission.h"
#include "core/v1_21/module_state.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/replies.h"
#include "core/v1_21/rewards.h"
#include "core/v1_21/scores.h"
#include "core/v1_21/summit.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

using obj::I;
using obj::Py;
using obj::U;
using pack::Packer;

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
const size_t CAR_AT = 0x2C;
constexpr size_t kBoostAt = 3 + 39;

bool head(Packer& pk, Py& py, int64_t status, Player& p, Raised* raised) {
  pk.H(status).I(p.level).I(p.xp).I(p.bucks).H(p.perk_points);
  if (pk.failed()) {
    *raised = pk.raised();
    return false;
  }
  Bytes esk;
  if (!summit::esk(py, p, &esk, raised)) return false;
  pk.raw(esk);
  return true;
}

bool block(Packer& pk, const Ctx& ctx, int64_t now, Raised* raised) {
  std::optional<int64_t> client_now = ctx.client_now_us(raised);
  if (!client_now) return false;
  std::pair<int64_t, int64_t> boost = summit::boost_wire(ctx.player, now, *client_now);
  pk.Qs(now).Q(NO_ID).B(0).I(0).Q(WINDOW_2023[0]).Q(WINDOW_2023[1]).B(3).B(0x14).B(boost.first).Qs(boost.second);
  pk.raw(wire::empty_array());
  return true;
}

std::optional<int64_t> cur_of(Ctx& ctx, Py& py) {
  Raised when;
  std::optional<int64_t> cur = summit::current(ctx.now_us(), gamedata::summit(), &when);
  if (!cur) {
    py.raise(when);
    return std::nullopt;
  }
  summit::sync(py, ctx.player, *cur, ctx.now_us(), gamedata::summit());
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
  bool entered = summit::enter(py, p, *cur, ctx.now_us(), gamedata::summit());
  if (py.failed()) return fail_of(py);
  if (entered) {
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  int64_t now = ctx.now_us();
  Raised r;
  std::optional<int64_t> client_now = ctx.client_now_us(&r);
  if (!client_now) return HandlerResult::fail(r);
  std::pair<int64_t, int64_t> boost = summit::boost_wire(p, now, *client_now);
  Packer pk;
  pk.H(0).B(1).I(p.bucks).Qs(now).Q(NO_ID).B(0).I(0).Q(WINDOW_2017[0]).Q(WINDOW_2017[1]).B(3).B(0x1E);
  pk.B(boost.first).Qs(boost.second).raw(wire::empty_array()).B(1).I(0);
  return reply_of(0x5BFA99B8, pk);
}

HandlerResult summit_event(Ctx& ctx, const Bytes& request, const Registered&) {
  const bool has_car = request.size() >= CAR_AT + 0x10;
  Bytes body = ljust(request, 0x50);
  uint64_t mission = be64(body, 0x1C);
  uint32_t score = be32(body, 0x25);
  Player& p = ctx.player;
  Py py;
  std::optional<int64_t> now_cur = cur_of(ctx, py);
  if (!now_cur) return fail_of(py);
  int64_t cur = *now_cur;
  std::optional<int64_t> slot = summit::event_slot(mission, cur, gamedata::summit());
  const bool pve = !slot && mission != 0;
  bool ok = pve;
  if (slot) {
    V* s = summit::state(py, p);
    if (!s) return fail_of(py);
    ok = obj::truthy(*s->get("entered"));
  }
  if (pve) {
    Raised r;
    std::optional<int64_t> part;
    if (!finish_mission(*ctx.core, p, mission, score, ctx.now_us(), true, &r, ctx.group_size, &body, CAR_AT, &part)) {
      return HandlerResult::fail(r);
    }
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  } else if (ok) {
    const rewards::Tables* t = rewards::tables(*ctx.core);
    std::pair<int64_t, int64_t> reward(0, 0);
    if (t) {
      Raised r;
      std::optional<std::pair<int64_t, int64_t>> got =
          rewards::mission_reward(*t, mission, score, p.level, &r, ctx.group_size);
      if (!got) return HandlerResult::fail(r);
      reward = *got;
    }
    reward = rewards::boosted(*ctx.core, p, ctx.now_us(), reward.first, reward.second);
    std::pair<V, V> got = rewards::allow(py, *ctx.core, p, ctx.now_us(), I(reward.first), I(reward.second));
    if (t) rewards::apply(py, *t, p, got.first, got.second, ctx.core->module_random());
    summit::post_score(py, p, *slot, score);
    if (py.failed()) return fail_of(py);
    V* s = summit::state(py, p);
    if (!s) return fail_of(py);
    s->set("played", obj::B(true));
    if (has_car) s->set("car", obj::list_of_u64({be64(body, CAR_AT), be64(body, CAR_AT + 8)}));
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  if (ok) scores::record(*ctx.core, mission, ctx.player_id, score, ctx.now_us());
  int64_t now = ctx.now_us();
  Packer pk;
  Raised r;
  if (!head(pk, py, ok ? 0 : 1, p, &r) || !block(pk, ctx, now, &r)) return HandlerResult::fail(r);
  pk.B(0).I(0x113).B(1).I(0x1E48C);
  if (ok) {
    pk.raw(wire::empty_array()).I(0).I(0).I(0x65);
  } else {
    pk.raw(wire::empty_array()).I(0).I(0xFFFFFFFF).I(0x65);
  }
  return reply_of(0xCF266AB3, pk);
}

HandlerResult summit_skill(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x50);
  uint64_t skill = be64(body, 0x1C);
  uint32_t score = be32(body, 0x24);
  Player& p = ctx.player;
  Py py;
  std::optional<int64_t> now_cur = cur_of(ctx, py);
  if (!now_cur) return fail_of(py);
  const rewards::Tables* t = rewards::tables(*ctx.core);
  bool ok = false;
  if (*now_cur != summit::NONE) {
    V* s = summit::state(py, p);
    if (!s) return fail_of(py);
    ok = obj::truthy(*s->get("entered")) && t && t->activities.count(skill);
  }
  if (ok) {
    const int64_t now = ctx.now_us();
    Raised r;
    std::optional<std::pair<int64_t, int64_t>> reward = rewards::summit_skill_reward(*t, skill, score, p.level, &r);
    if (!reward) return HandlerResult::fail(r);
    *reward = rewards::boosted(*ctx.core, p, now, reward->first, reward->second, std::nullopt);
    std::pair<V, V> got = rewards::allow(py, *ctx.core, p, now, I(reward->first), I(reward->second));
    rewards::apply(py, *t, p, got.first, got.second, ctx.core->module_random());
    if (py.failed()) return fail_of(py);
    p.complete_activity(py, skill);
    if (py.failed()) return fail_of(py);
    V* s = summit::state(py, p);
    if (!s) return fail_of(py);
    s->set("played", obj::B(true));
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  int64_t now = ctx.now_us();
  Packer pk;
  pk.H(ok ? 0 : 1).I(p.level).I(p.xp).I(p.bucks).I(p.perk_points);
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  Raised r;
  Bytes esk;
  if (!summit::esk(py, p, &esk, &r)) return HandlerResult::fail(r);
  pk.raw(esk);
  if (!block(pk, ctx, now, &r)) return HandlerResult::fail(r);
  pk.B(0).I(0).B(0).I(0).raw(wire::empty_array()).I(0).I(0).I(0);
  return reply_of(0x917CD25C, pk);
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
  int64_t now = ctx.now_us();
  std::pair<int64_t, int64_t> pay = rewards::boosted(*ctx.core, p, now, 0, 1, std::nullopt);
  std::pair<V, V> got = rewards::allow(py, *ctx.core, p, now, I(pay.first), I(pay.second));
  if (const rewards::Tables* t = rewards::tables(*ctx.core)) {
    rewards::add_bucks(py, *t, p, got.second);
  } else {
    p.bucks = py.add(p.bucks, got.second, true);
  }
  if (py.failed()) return fail_of(py);
  V entered = *s->get("entered");
  summit::result(py, p, rank, cur, now, gamedata::summit());
  if (py.failed()) return fail_of(py);
  std::optional<summit::Finished> finished = summit::take_finished(*summit::state(py, p), gamedata::summit());
  if (finished && finished->played) {
    summit::spin(py, *ctx.core, p, finished->index, finished->year, now, ctx.core->module_random(), gamedata::summit(),
                 finished->car);
    if (py.failed()) return fail_of(py);
  }
  if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  Packer pk;
  pk.H(0).I(p.bucks);
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  Raised r;
  Bytes esk;
  if (!summit::esk(py, p, &esk, &r)) return HandlerResult::fail(r);
  pk.raw(esk);
  if (!block(pk, ctx, now, &r)) return HandlerResult::fail(r);
  pk.B(1).I(0x1CB).B(1).I(0x258).B(entered).B(0).I(0).I(0x35);
  return reply_of(0xCCF7FCFC, pk);
}

constexpr double SKILL_REPUTATION = 20.0;
constexpr double SKILL_VALUE_CAP = 250.0;

int64_t skill_reputation(uint32_t value) {
  double v = pack::f32_of_bits(value);
  if (!(v > 0.0 && v < HUGE_VAL)) return 0;
  return static_cast<int64_t>(rewards::f32(std::min(v, SKILL_VALUE_CAP) * SKILL_REPUTATION));
}

HandlerResult summit_status(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x50);
  Player& p = ctx.player;
  Py py;
  std::optional<int64_t> now_cur = cur_of(ctx, py);
  if (!now_cur) return fail_of(py);
  const int64_t cur = *now_cur;
  V* s = summit::state(py, p);
  if (!s) return fail_of(py);
  const int64_t now = ctx.now_us();
  const uint32_t value = be32(body, 0x1E);
  Raised r;
  V key = V::null();
  if (cur != summit::NONE) {
    std::optional<int64_t> year = summit::occurrence(cur, now, *gamedata::summit(), &r);
    if (!year) return HandlerResult::fail(r);
    key = obj::list_of({cur, *year});
  }
  const V* last_v = s->get("value");
  int64_t last = 0;
  if (last_v->is_int()) {
    std::optional<int64_t> v = obj::small_int(*last_v);
    if (v && *v >= 0 && *v < (int64_t(1) << 32)) last = *v;
  }
  const V* had = s->get("value_occurrence");
  const int64_t before = !had || obj::eq(*had, key) ? skill_reputation(static_cast<uint32_t>(last)) : 0;
  const int64_t after = skill_reputation(value);
  s->set("value", I(value));
  s->set("value_occurrence", key);
  V results = py.add(*s->get("results"), I(1), true);
  if (py.failed()) return fail_of(py);
  s->set("results", std::move(results));
  std::optional<int64_t> credited = factions::add_rep(*ctx.core, p, factions::NO_ZONE,
                                                      after - (before <= after ? before : 0),
                                                      py::floor_div(now, 1000000), &r);
  if (!credited) return HandlerResult::fail(r);
  if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  Packer pk;
  if (!head(pk, py, 0, p, &r) || !block(pk, ctx, now, &r)) return HandlerResult::fail(r);
  s = summit::state(py, p);
  pk.B(1).I(0x31).B(1).I(*credited).I(0).B(*s->get("entered")).B(0).I(0).I(3).I(0x172C);
  pk.Q(ctx.player_id);
  return reply_of(0xE4EA417B, pk);
}

HandlerResult summit_player(Ctx& ctx, const Bytes& request, const Registered&) {
  const gamedata::AutoReply* a = gamedata::auto_reply(0xEA3A66D7);
  if (!a) return HandlerResult::fail(Raised{"KeyError", "3929712343"});
  Bytes out = codec::encode_default(a->layout).value_or(Bytes());
  Bytes body = ljust(request, 0x1D);
  if (ctx.verified && be64(body, 0x14) == ctx.player_id) {
    Raised r;
    std::optional<int64_t> client_now = ctx.client_now_us(&r);
    int64_t now = ctx.now_us();
    if (!client_now) return HandlerResult::fail(r);
    std::pair<int64_t, int64_t> boost = summit::boost_wire(ctx.player, now, *client_now);
    Packer pk;
    pk.B(boost.first).Qs(boost.second);
    if (pk.failed()) return HandlerResult::fail(pk.raised());
    if (out.size() < kBoostAt + 9) {
      return HandlerResult::fail(pack::error("pack_into requires a buffer of at least " +
                                             py::uint_to_text(kBoostAt + 9) + " bytes for packing 9 bytes at offset " +
                                             py::uint_to_text(kBoostAt) + " (actual buffer size is " +
                                             py::uint_to_text(out.size()) + ")"));
    }
    std::copy(pk.out().begin(), pk.out().end(), out.begin() + static_cast<std::ptrdiff_t>(kBoostAt));
  }
  return HandlerResult::reply(a->reply, std::move(out));
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

int64_t seconds_of(const Ctx& ctx) { return py::floor_div(ctx.now_us(), 1000000); }

bool faction_block(const Ctx& ctx, const factions::State& state, int64_t ok, Bytes* out, Raised* raised) {
  const Player& p = ctx.player;
  const int64_t now = ctx.now_us();
  const int64_t g = factions::faction_of(p);
  const int64_t s = factions::score(p, py::floor_div(now, 1000000));
  int64_t stand = factions::NO_STANDING;
  std::vector<int64_t> pairs(12, 0);
  if (g) {
    const factions::Faction& rec = state.factions[static_cast<size_t>(g - 1)];
    stand = factions::standing(rec, s);
    const auto r = factions::ranks(rec);
    for (size_t i = 0; i < r.size(); i++) {
      pairs[2 * i] = r[i].first;
      pairs[2 * i + 1] = r[i].second;
    }
  }
  Packer pk;
  pk.B(ok).Q(ctx.player_id).B(g).Qs(now).Q(factions::joined_us(p)).I(s).I(stand);
  for (int64_t v : factions::average_salaries(state)) pk.I(v);
  for (int64_t v : pairs) pk.I(v);
  for (int64_t v : factions::zone_scores(state)) pk.Qs(v);
  if (pk.failed()) {
    *raised = pk.raised();
    return false;
  }
  *out = std::move(pk.out());
  return true;
}

HandlerResult group_join(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x1E);
  Player& p = ctx.player;
  const int64_t now = ctx.now_us();
  const int64_t g = body[0x1C];
  const bool ok = g <= static_cast<int64_t>(factions::GROUPS) && g != factions::faction_of(p);
  if (ok) {
    p.group = I(g);
    p.group_joined = I(now);
    p.faction = V::object();
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  const int64_t own = factions::faction_of(p);
  Packer pk;
  pk.B(ok ? 1 : 0).Q(ctx.player_id).B(own);
  if (own) {
    pk.Q(factions::joined_us(p));
  } else {
    pk.Qs(now);
  }
  pk.Qs(now).I(factions::score(p, py::floor_div(now, 1000000))).I(factions::NO_STANDING);
  return reply_of(0x9FD2A05A, pk);
}

HandlerResult faction_mission(Ctx& ctx, const Bytes& request, const Registered&) {
  Bytes body = ljust(request, 0x12);
  Player& p = ctx.player;
  Raised r;
  if (!pay_faction_mission(*ctx.core, p, body, ctx.now_us(), ctx.group_size, &r)) return HandlerResult::fail(r);
  if (be64(body, 0x08)) {
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  std::optional<factions::State> state = factions::load(*ctx.core, seconds_of(ctx), &r);
  if (!state) return HandlerResult::fail(r);
  Bytes head;
  if (!faction_block(ctx, *state, 1, &head, &r)) return HandlerResult::fail(r);
  Packer pk;
  pk.raw(head).I(p.level).I(p.xp).I(p.bucks).I(factions::score(p, seconds_of(ctx))).I(p.perk_points);
  pk.raw(wire::empty_array()).I(0);
  return reply_of(0xB41B0F92, pk);
}

HandlerResult daily_reward(Ctx& ctx, const Bytes&, const Registered&) {
  const gamedata::ReplyTemplate* t = gamedata::reply_template(0xA7476B7D);
  if (!t) return HandlerResult::fail(Raised{"KeyError", "'0xa7476b7d'"});
  Player& p = ctx.player;
  const int64_t now = seconds_of(ctx);
  Raised r;
  std::optional<factions::State> state = factions::load(*ctx.core, now, &r);
  if (!state) return HandlerResult::fail(r);
  const int64_t g = factions::faction_of(p);
  const int64_t s = factions::score(p, now);
  const factions::Faction* rec = g ? &state->factions[static_cast<size_t>(g - 1)] : nullptr;
  const int64_t stand = rec ? factions::standing(*rec, s) : factions::NO_STANDING;
  const V last = p.daily_last.is_int() ? p.daily_last : I(0);
  Py py;
  const bool due = obj::eq(last, I(0)) || py.ge(py.sub(I(now), last), I(factions::DAY));
  if (py.failed()) return fail_of(py);
  int64_t paid = 0;
  if (due) {
    p.daily_last = I(now);
    paid = rec ? factions::salary(factions::rank_of(*rec, s, stand)) : 0;
    if (const rewards::Tables* tables = rewards::tables(*ctx.core)) {
      rewards::add_bucks(py, *tables, p, I(paid));
    } else {
      p.bucks = py.add(p.bucks, I(paid), true);
    }
    if (py.failed()) return fail_of(py);
    if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  }
  Bytes head;
  if (!faction_block(ctx, *state, 1, &head, &r)) return HandlerResult::fail(r);
  Bytes out = t->body;
  if (out.size() < 0x147 + 200) return HandlerResult::fail(pack::error("the salary template is too short"));
  out[0] = 1;
  out[1] = 1;
  Packer bucks;
  bucks.I(p.bucks);
  if (bucks.failed()) return HandlerResult::fail(bucks.raised());
  std::copy(bucks.out().begin(), bucks.out().end(), out.begin() + 0x02);
  std::copy(head.begin() + 0x22, head.begin() + 0x66, out.begin() + 0x06);
  for (size_t k = 0; k < 8; k++) out[0x4A + k] = static_cast<uint8_t>(ctx.player_id >> (8 * k));
  std::copy(head.begin() + 0x09, head.begin() + 0x22, out.begin() + 0x52);
  std::copy(head.begin() + 0x66, head.begin() + 0x12E, out.begin() + 0x6B);
  Packer tail;
  tail.I(paid).I(s);
  Packer clock;
  clock.Qs(ctx.now_us());
  Packer months;
  for (int64_t v : factions::last_month_scores(*state)) months.Qs(v);
  if (clock.failed()) return HandlerResult::fail(clock.raised());
  std::copy(tail.out().begin(), tail.out().end(), out.begin() + 0x133);
  std::copy(clock.out().begin(), clock.out().end(), out.begin() + 0x13F);
  std::copy(months.out().begin(), months.out().end(), out.begin() + 0x147);
  return HandlerResult::reply(t->type, std::move(out));
}

HandlerResult group_status(Ctx& ctx, const Bytes&, const Registered&) {
  Raised r;
  std::optional<factions::State> state = factions::load(*ctx.core, seconds_of(ctx), &r);
  if (!state) return HandlerResult::fail(r);
  Bytes out;
  if (!faction_block(ctx, *state, 1, &out, &r)) return HandlerResult::fail(r);
  return HandlerResult::reply(0xC0CFC1C9, std::move(out));
}

HandlerResult group_status2(Ctx& ctx, const Bytes&, const Registered&) {
  Raised r;
  std::optional<factions::State> state = factions::load(*ctx.core, seconds_of(ctx), &r);
  if (!state) return HandlerResult::fail(r);
  Bytes out;
  if (!faction_block(ctx, *state, 1, &out, &r)) return HandlerResult::fail(r);
  out.erase(out.begin());
  return HandlerResult::reply(0x3D3D4841, std::move(out));
}

constexpr double HEAT_CAP = 100000.0;
constexpr int64_t WANTED_CAP = 100000;
constexpr double HEAT_REPAIR = 10 * HEAT_CAP;
constexpr int64_t RACER_CHUNKS = 10;
constexpr size_t STATS_SIZE_AT = 0x52;

std::optional<int64_t> crate_quality(const Bytes& body) {
  if (be64(body, 0x24) == NO_ID) return std::nullopt;
  uint64_t n = be32(body, STATS_SIZE_AT);
  if (n <= 0x10 || 0x4A + n + 4 > body.size()) return std::nullopt;
  double f = pack::f32_of_bits(be32(body, static_cast<size_t>(0x4A + n)));
  if (f == 32.0) return 0;
  if (f == 48.0) return 1;
  if (f == 64.0) return 2;
  if (f == 80.0) return 3;
  return std::nullopt;
}

uint64_t pick_racer(const rewards::Chase& ch, const std::set<uint64_t>& owned, Random& rng) {
  std::vector<std::pair<uint64_t, int64_t>> cands;
  int64_t total = 0;
  for (const auto& e : ch.racer_cars) {
    if (owned.count(e.first)) continue;
    cands.push_back(e);
    total += e.second;
  }
  if (total <= 0) return NO_ID;
  int64_t pick = rng.randrange(total).value_or(0);
  for (const auto& e : cands) {
    if (pick < e.second) return e.first;
    pick -= e.second;
  }
  return NO_ID;
}

uint64_t pick_police(const rewards::Chase& ch, const std::set<uint64_t>& owned, int64_t level, Random& rng) {
  std::vector<uint64_t> cands;
  for (const auto& e : ch.police_cars) {
    if (e.second <= level && !owned.count(e.first)) cands.push_back(e.first);
  }
  if (cands.empty()) return NO_ID;
  return cands[static_cast<size_t>(rng.randrange(static_cast<int64_t>(cands.size())).value_or(0))];
}

struct Cau {
  int64_t xp = 0, bucks = 0, won_chunk = 0;
  uint64_t won = NO_ID;
};

bool cau(Ctx& ctx, bool escaped, std::optional<int64_t> quality, Cau* out, Raised* raised) {
  const rewards::Tables* t = rewards::tables(*ctx.core);
  if (!t || !t->chase.present) return true;
  const rewards::Chase& ch = t->chase;
  Player& p = ctx.player;
  Random& rng = ctx.core->module_random();
  uint64_t racer = chase_car(p.chase_racer_car), police = chase_car(p.chase_police_car);
  if (racer == NO_ID || police == NO_ID) {
    std::optional<std::set<uint64_t>> owned = cars::owned_variants(p, raised);
    if (!owned) return false;
    if (racer == NO_ID) {
      racer = pick_racer(ch, *owned, rng);
      p.chase_racer_car = U(racer);
      p.chase_racer_chunks = I(0);
    }
    if (police == NO_ID) {
      p.chase_police_car = U(pick_police(ch, *owned, p.level, rng));
      p.chase_police_chunks = I(0);
    }
  }
  if (!escaped || !quality) return true;
  int64_t top = t->table.size() > rewards::TOP_LEVEL ? t->table[rewards::TOP_LEVEL] : 0;
  int64_t lvl = std::min(std::max(p.level, int64_t(1)), top);
  const size_t q = static_cast<size_t>(*quality);
  if (q >= ch.crate_values.size() || q >= ch.chunk_percent.size()) {
    *raised = Raised{"IndexError", "list index out of range"};
    return false;
  }
  std::optional<double> xs = t->f(rewards::XP_BASE + lvl - 1, raised);
  if (!xs) return false;
  out->xp = static_cast<int64_t>(rewards::f32(ch.crate_values[q].first * *xs));
  std::optional<double> bs = t->f(rewards::BUCKS_BASE + lvl - 1, raised);
  if (!bs) return false;
  out->bucks = static_cast<int64_t>(rewards::f32(ch.crate_values[q].second * *bs));
  if (racer != NO_ID && rng.randrange(100).value_or(0) < ch.chunk_percent[q]) {
    out->won_chunk = 1;
    int64_t chunks = chase_chunks(p.chase_racer_chunks) + 1;
    p.chase_racer_chunks = I(chunks);
    if (chunks >= RACER_CHUNKS) {
      std::optional<bool> granted = cars::grant(*ctx.core, p, racer, raised);
      if (!granted) return false;
      if (*granted) out->won = racer;
      p.chase_racer_chunks = I(0);
      std::optional<std::set<uint64_t>> owned = cars::owned_variants(p, raised);
      if (!owned) return false;
      p.chase_racer_car = U(pick_racer(ch, *owned, rng));
    }
  }
  return true;
}

V chase_seconds(Py& py, uint64_t start, int64_t clock) { return py.max(py.sub(I(clock), U(start)), I(0)); }

}

bool repair_heat(Py& py, Player& p) {
  if (!(p.heat.is_int() || p.heat.is_float()) || !py.gt(p.heat, obj::F(HEAT_REPAIR))) return false;
  p.heat = obj::F(1.0);
  return true;
}

namespace {

V chase(Py& py, Ctx& ctx, bool escaped, int64_t wanted_in, uint64_t start, int64_t now, int64_t clock, const Cau& c,
        V* crate) {
  Player& p = ctx.player;
  const V h = py.min(p.heat, obj::F(HEAT_CAP));
  const V wanted = I(std::min(wanted_in, WANTED_CAP));
  V xp = py.add(py.add(py.to_int(py.mul(wanted, obj::F(0.4))), I(100)), py.to_int(py.mul(obj::F(0.05), h)));
  V bonus = I(0), fine = I(0), bucks;
  if (escaped) {
    bonus = py.to_int(py.mul(obj::F(0.2), h));
    bucks = py.add(py.add(py.add(py.to_int(py.mul(wanted, obj::F(0.5))), I(500)), bonus), I(c.bucks));
    xp = py.add(xp, I(c.xp));
    V grown = py.add(py.mul(chase_seconds(py, start, clock), obj::F(0.5)), py.mul(wanted, obj::F(0.2)));
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
  *crate = I(0);
  if (escaped) {
    bonus = py.min(bonus, bucks);
    *crate = py.min(I(c.bucks), bucks);
  }
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
  Bytes body = ljust(request, STATS_SIZE_AT + 4);
  Player& p = ctx.player;
  uint64_t start = be64(body, 0x34);
  uint32_t wanted = be32(body, 0x3C);
  const bool escaped = body[0x41] != 0;
  Cau c;
  Raised r;
  if (!cau(ctx, escaped, crate_quality(body), &c, &r)) return HandlerResult::fail(r);
  Py py;
  V crate;
  std::optional<int64_t> client_now = ctx.client_now_us(&r);
  if (!client_now) return HandlerResult::fail(r);
  V value = chase(py, ctx, escaped, wanted, start, py::floor_div(ctx.now_us(), 1000000),
                  py::floor_div(*client_now, 1000000), c, &crate);
  if (py.failed()) return fail_of(py);
  if (std::optional<Raised> failed = p.save()) return HandlerResult::fail(*failed);
  Bytes array;
  wire::put_u32(array, 1);
  Bytes rt = rating(ctx.player_id);
  array.insert(array.end(), rt.begin(), rt.end());
  Bytes ratings = wire::blob(wire::frame(wire::T_ARRAY, array));
  Bytes ten;
  wire::put_u32(ten, 10);
  Packer pk;
  pk.H(0).I(p.level).I(p.xp).I(p.bucks).I(p.perk_points);
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  pk.Q(chase_car(p.chase_racer_car)).B(chase_chunks(p.chase_racer_chunks)).Q(chase_car(p.chase_police_car));
  pk.B(chase_chunks(p.chase_police_chunks)).Q(uint64_t(0)).raw(wire::empty_array());
  pk.Q(NO_ID).raw(wire::blob(wire::frame(wire::T_ARRAY, ten))).raw(wire::empty_array());
  pk.B(c.won_chunk).Q(c.won).B(0).I(0x8B3).I(crate).I(300).I(value).I(0x68).raw(wire::empty_array());
  pk.I(0).raw(wire::empty_array()).raw(wire::empty_array()).raw(ratings).raw(ratings).H(0).B(0);
  return reply_of(0x2C81736C, pk);
}

}

bool pay_faction_mission(Core& core, Player& p, const Bytes& body, int64_t now_us, int64_t group, Raised* raised) {
  const uint64_t mission = be64(body, 0x08);
  const int64_t zone = body[0x11];
  if (!mission) return true;
  int64_t rep = 0;
  if (const rewards::Tables* t = rewards::tables(core)) {
    std::optional<int64_t> got = rewards::mission_reputation(*t, mission, p.level, raised, group);
    if (!got) return false;
    rep = *got;
  }
  if (!finish_mission(core, p, mission, NO_SCORE, now_us, true, raised, group, &body, FACTION_CAR_AT)) return false;
  rep = rewards::boosted_reputation(core, p, now_us, rep);
  const int64_t into = zone < static_cast<int64_t>(factions::ZONES) ? zone : factions::NO_ZONE;
  return factions::add_rep(core, p, into, rep, py::floor_div(now_us, 1000000), raised).has_value();
}

void register_crew(std::vector<Registered>& out) {
  add(out, 0x9868D8CE, "PLAYER_SET_GROUP", set_group);
  add(out, 0x7FA3E7E6, "GROUP_REGISTER", register_group);
  add(out, 0x80A06106, "GROUP_KEEP_ALIVE", keep_alive);
  add(out, 0x33065129, "GROUP_LEAVE", leave);
}

void register_group(std::vector<Registered>& out) {
  add(out, 0x7932CE3E, "GROUP_JOIN", group_join);
  add(out, 0xAD26AF59, "FACTION_MISSION", faction_mission);
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
  add(out, 0x6280DC9B, "SUMMIT_SKILL", summit_skill);
  add(out, 0xB9BA9057, "SUMMIT_RESULT", summit_result);
  add(out, 0x115C657D, "SUMMIT_STATUS", summit_status);
  add(out, 0xD14A2746, "SUMMIT_SCORES", summit_scores);
  add(out, 0xEA3A66D7, "SUMMIT_PLAYER", summit_player);
  add(out, 0xF282A24C, "SAVE_IDS", save_ids);
}

}
}
