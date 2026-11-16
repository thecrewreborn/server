#include "core/v1_21/rewards.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "core/core.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/module_state.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/summit.h"

namespace tcr {
namespace v1_21 {
namespace rewards {

using gamedata::float_of;
using gamedata::int_of;
using gamedata::ints_of;
using gamedata::str_of;

namespace {

const int64_t kLevelUpChance[] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20};
constexpr int64_t kLevelUpChances = sizeof(kLevelUpChance) / sizeof(kLevelUpChance[0]);

Raised index_error() { return Raised{"IndexError", "list index out of range"}; }

bool py_index(int64_t i, size_t size, size_t* out) {
  int64_t n = static_cast<int64_t>(size);
  if (i < 0) i += n;
  if (i < 0 || i >= n) return false;
  *out = static_cast<size_t>(i);
  return true;
}

std::unique_ptr<Tables> load() {
  const json::Value* d = gamedata::table("rewards.json");
  if (!d || !d->is_object()) return nullptr;
  std::unique_ptr<Tables> t(new Tables());
  t->table = ints_of(d->get("table"));
  if (const json::Value* rv = d->get("reward_values")) {
    for (const json::Member& m : rv->members()) t->reward_values[gamedata::hex_id(m.key)] = ints_of(&m.value);
  }
  if (const json::Value* ms = d->get("missions")) {
    for (const json::Member& e : ms->members()) {
      const json::Value& v = e.value;
      Mission m;
      m.online = int_of(v.get("online"));
      m.min_level = int_of(v.get("min_level"));
      m.max_level = int_of(v.get("max_level"));
      const json::Value* rewarding = v.get("rewarding");
      m.rewarding = rewarding && truthy(*rewarding);
      m.factor = float_of(v.get("factor"));
      m.has_thresholds = v.get("thresholds") != nullptr;
      m.thresholds = ints_of(v.get("thresholds"));
      m.reward_values = gamedata::hex_id(str_of(v.get("reward_values")));
      m.cc_gift = int_of(v.get("cc_gift"));
      if (const json::Value* names = v.get("abilities")) {
        for (const json::Value& n : names->items()) m.abilities.push_back(str_of(&n));
      }
      t->missions[gamedata::hex_id(e.key)] = std::move(m);
    }
  }
  if (const json::Value* ab = d->get("abilities")) {
    for (const json::Member& m : ab->members()) t->abilities[m.key] = int_of(&m.value);
  }
  if (const json::Value* hidden = d->get("hidden")) {
    for (const json::Value& n : hidden->items()) t->hidden.insert(str_of(&n));
  }
  if (const json::Value* acts = d->get("activities")) {
    for (const json::Member& e : acts->members()) {
      Activity a;
      a.region = gamedata::hex_id(str_of(e.value.get("region")));
      a.family = gamedata::hex_id(str_of(e.value.get("family")));
      a.thresholds = ints_of(e.value.get("thresholds"));
      t->activities[gamedata::hex_id(e.key)] = std::move(a);
    }
  }
  if (const json::Value* regions = d->get("regions")) {
    for (const json::Member& e : regions->members()) {
      Region r;
      r.level = int_of(e.value.get("level"));
      r.max = int_of(e.value.get("max"));
      r.xp = float_of(e.value.get("xp"));
      r.bucks = float_of(e.value.get("bucks"));
      uint64_t key = gamedata::hex_id(e.key);
      if (!t->regions.count(key)) t->region_order.push_back(key);
      t->regions[key] = r;
    }
  }
  if (const json::Value* families = d->get("families")) {
    for (const json::Member& e : families->members()) {
      std::vector<double> f;
      for (const json::Value& x : e.value.items()) f.push_back(float_of(&x));
      t->families[gamedata::hex_id(e.key)] = std::move(f);
    }
  }
  if (const json::Value* ch = d->get("chase")) {
    t->chase.present = ch->is_object() && !ch->members().empty();
    t->chase.chunk_percent = ints_of(ch->get("chunk_percent"));
    if (const json::Value* cv = ch->get("crate_values")) {
      for (const json::Value& x : cv->items()) {
        const auto& f = x.items();
        t->chase.crate_values.emplace_back(f.size() > 0 ? float_of(&f[0]) : 0.0, f.size() > 1 ? float_of(&f[1]) : 0.0);
      }
    }
    for (const char* key : {"racer_cars", "police_cars"}) {
      auto& out = std::string(key) == "racer_cars" ? t->chase.racer_cars : t->chase.police_cars;
      if (const json::Value* list = ch->get(key)) {
        for (const json::Value& x : list->items()) {
          const auto& f = x.items();
          if (f.size() < 2) continue;
          out.emplace_back(gamedata::hex_id(str_of(&f[0])), int_of(&f[1]));
        }
      }
    }
  }
  if (const json::Value* ic = d->get("instant")) {
    for (const json::Value& x : ic->get("xp") ? ic->get("xp")->items() : std::vector<json::Value>()) {
      t->instant_xp.push_back(float_of(&x));
    }
    for (const json::Value& x : ic->get("bucks") ? ic->get("bucks")->items() : std::vector<json::Value>()) {
      t->instant_bucks.push_back(float_of(&x));
    }
  }
  if (const json::Value* coop = d->get("coop")) {
    for (const json::Member& e : coop->members()) {
      std::vector<double> f;
      for (const json::Value& x : e.value.items()) f.push_back(float_of(&x));
      t->coop[py::int_text_to_i64(py::parse_int(e.key).value_or("0")).value_or(0)] = std::move(f);
    }
  }
  if (const json::Value* lower = d->get("lower_better")) {
    for (const json::Value& x : lower->items()) t->lower_better.insert(gamedata::hex_id(str_of(&x)));
  }
  if (const json::Value* boosts = d->get("perk_boosts")) {
    for (const json::Member& e : boosts->members()) {
      std::vector<double> f;
      for (const json::Value& x : e.value.items()) f.push_back(float_of(&x));
      t->perk_boosts[py::int_text_to_i64(py::parse_int(e.key).value_or("0")).value_or(0)] = std::move(f);
    }
  }
  return t;
}

int64_t xp_next(const Tables& t, int64_t level) {
  if (level - 1 >= 0 && level - 1 < 60) {
    size_t i = static_cast<size_t>(XP_NEXT + level - 1);
    return i < t.table.size() ? t.table[i] : 0;
  }
  return 0xFFFFFFFFll;
}

}

std::optional<double> Tables::f(int64_t i, Raised* raised) const {
  size_t k = 0;
  if (!py_index(i, table.size(), &k)) {
    *raised = index_error();
    return std::nullopt;
  }
  return pack::f32_of_bits(static_cast<uint32_t>(table[k]));
}

const Tables* tables(const Core& core) {
  static const std::unique_ptr<Tables> loaded = load();
  if (!core.overrides().reward_tables) return nullptr;
  return loaded.get();
}

double f32(double x) { return static_cast<double>(static_cast<float>(x)); }

std::optional<int64_t> medal(const Tables& t, const std::vector<int64_t>& thresholds, int64_t score, int64_t level,
                             Raised* raised) {
  int64_t m = 0;
  for (int64_t i : {3, 2, 1, 0}) {
    size_t k = 0;
    if (!py_index(i, thresholds.size(), &k)) {
      *raised = index_error();
      return std::nullopt;
    }
    if (thresholds[k] <= score) {
      m = i;
      break;
    }
  }
  return m == 3 && level < t.max_level() ? 2 : m;
}

bool player_level_type(int64_t online) {
  return online == 4 || online == 5 || online == 6 || online == 10 || online == 11;
}

int64_t coop_bonus(const Tables& t, int64_t amount, int64_t players, int64_t rank) {
  auto factors = players >= 2 && players <= 4 ? t.coop.find(players) : t.coop.end();
  double f = 0.0;
  if (factors != t.coop.end() && rank >= 1 && rank <= std::min<int64_t>(players, factors->second.size())) {
    f = factors->second[static_cast<size_t>(rank - 1)];
  }
  if (f == 0.0) return amount;
  int64_t bonus = static_cast<int64_t>(f32(f32(static_cast<double>(amount)) * f32(f - 1.0)));
  return static_cast<int64_t>(f32(f32(static_cast<double>(amount)) + f32(static_cast<double>(bonus))));
}

std::optional<std::pair<int64_t, int64_t>> client_reward(const Tables& t, const Mission& m, int64_t score,
                                                         int64_t level, Raised* raised, int64_t group) {
  const std::pair<int64_t, int64_t> none(0, 0);
  int64_t top = t.table.size() > TOP_LEVEL ? t.table[TOP_LEVEL] : 0;
  int64_t lvl = std::min(std::max(player_level_type(m.online) ? level : m.max_level, int64_t(1)), top);
  auto rv = t.reward_values.find(m.reward_values);
  if (rv == t.reward_values.end()) return none;
  if (rv->second.size() < 2) {
    *raised = Raised{"ValueError", "not enough values to unpack (expected 2, got " +
                                       std::to_string(rv->second.size()) + ")"};
    return std::nullopt;
  }
  double xp_factor = pack::f32_of_bits(static_cast<uint32_t>(rv->second[0])),
         bucks_factor = pack::f32_of_bits(static_cast<uint32_t>(rv->second[1]));
  std::optional<double> x1 = t.f(XP_BASE + lvl - 1, raised);
  if (!x1) return std::nullopt;
  int64_t xp = static_cast<int64_t>(f32(xp_factor * *x1));
  std::optional<double> b1 = t.f(BUCKS_BASE + lvl - 1, raised);
  if (!b1) return std::nullopt;
  int64_t bucks = static_cast<int64_t>(f32(bucks_factor * *b1));
  if (m.online == EVENT) {
    double k = (4.5 - static_cast<double>(EVENT_RANK)) * 0.01;
    k = k + 1.0;
    xp = static_cast<int64_t>(f32(k * static_cast<double>(xp)));
    bucks = static_cast<int64_t>(f32(k * static_cast<double>(bucks)));
  } else {
    xp = coop_bonus(t, xp, group);
    bucks = coop_bonus(t, bucks, group);
  }
  int64_t md = 0;
  if (m.thresholds.size() >= 4) {
    std::optional<int64_t> got = medal(t, m.thresholds, score, level, raised);
    if (!got) return std::nullopt;
    md = *got;
  }
  std::optional<double> x2 = t.f(XP_MEDAL + md, raised);
  if (!x2) return std::nullopt;
  xp = static_cast<int64_t>(f32(f32(static_cast<double>(xp) * *x2) * m.factor));
  std::optional<double> b2 = t.f(BUCKS_MEDAL + md, raised);
  if (!b2) return std::nullopt;
  bucks = static_cast<int64_t>(f32(f32(static_cast<double>(bucks) * *b2) * m.factor));
  int64_t fade = std::min(std::max(level - lvl - 10, int64_t(0)), int64_t(10));
  xp -= static_cast<int64_t>(f32(f32(static_cast<double>(fade) * f32(static_cast<double>(xp))) * f32(0.1)));
  return std::make_pair(xp, bucks);
}

std::optional<std::pair<int64_t, int64_t>> mission_reward(const Tables& t, uint64_t mission, int64_t score,
                                                          int64_t level, Raised* raised, int64_t group) {
  const std::pair<int64_t, int64_t> none(0, 0);
  auto it = t.missions.find(mission);
  if (it == t.missions.end()) return none;
  const Mission& m = it->second;
  if (level < m.min_level) return none;
  if ((m.online == EVENT && !PAY_EVENTS) || (m.online == SUMMIT && !PAY_SUMMIT)) return none;
  std::optional<std::pair<int64_t, int64_t>> r = client_reward(t, m, score, level, raised, group);
  if (!r) return std::nullopt;
  if (!m.rewarding) r->first = 0;
  return r;
}

double perk_boost(const Tables* t, const Player& p, int64_t perk) {
  if (!t) return 0.0;
  auto values = t->perk_boosts.find(perk);
  if (values == t->perk_boosts.end()) return 0.0;
  int64_t level = 0;
  if (p.perks.is_array() && perk >= 0 && static_cast<size_t>(perk) < p.perks.items().size()) {
    const V& item = p.perks.items()[static_cast<size_t>(perk)];
    if (item.is_int()) level = item.as_i64().value_or(0);
  }
  const std::vector<double>& v = values->second;
  return level >= 1 && level <= static_cast<int64_t>(v.size()) ? v[static_cast<size_t>(level - 1)] : 0.0;
}

std::pair<int64_t, int64_t> boosted(const Core& core, const Player& p, int64_t now_us, int64_t xp, int64_t bucks,
                                    std::optional<int64_t> perk) {
  if (std::optional<double> factor = summit::boost_factor(p, now_us, gamedata::summit())) {
    if (xp > 0) xp = static_cast<int64_t>(f32(f32(static_cast<double>(xp)) * *factor));
    if (bucks > 0) bucks = static_cast<int64_t>(f32(f32(static_cast<double>(bucks)) * *factor));
  }
  double v = perk ? perk_boost(tables(core), p, *perk) : 0.0;
  if (v != 0.0) {
    double k = v * PERK_FACTOR;
    if (xp > 0) xp += static_cast<int64_t>(k * f32(static_cast<double>(xp)));
    if (bucks > 0) bucks += static_cast<int64_t>(k * f32(static_cast<double>(bucks)));
  }
  return {xp, bucks};
}

bool faction_type(int64_t online) { return online == 4 || online == 5 || online == 6; }

int64_t crew_reputation(int64_t rep, int64_t players, int64_t rank) {
  if (!(1 <= rank && rank <= players && players <= CREW_MAX)) return rep;
  double k = f32(f32(f32(static_cast<double>(players - rank)) * f32(CREW_REPUTATION_STEP)) + 1.0);
  return static_cast<int64_t>(f32(k * f32(static_cast<double>(rep))));
}

std::optional<int64_t> mission_reputation(const Tables& t, uint64_t mission, int64_t level, Raised* raised,
                                          int64_t group) {
  auto it = t.missions.find(mission);
  if (it == t.missions.end()) return 0;
  const Mission& m = it->second;
  if (!faction_type(m.online) || level < m.min_level) return 0;
  if (m.online == FACTION_BONUS && !PAY_BONUS_REPUTATION) return 0;
  auto rv = t.reward_values.find(m.reward_values);
  if (rv == t.reward_values.end()) return 0;
  if (rv->second.size() <= REPUTATION_FACTOR) {
    *raised = index_error();
    return std::nullopt;
  }
  int64_t top = t.table.size() > TOP_LEVEL ? t.table[TOP_LEVEL] : 0;
  int64_t lvl = std::min(std::max(level, int64_t(1)), top);
  std::optional<double> per_level = t.f(REPUTATION_BASE + lvl - 1, raised);
  if (!per_level) return std::nullopt;
  double factor = pack::f32_of_bits(static_cast<uint32_t>(rv->second[REPUTATION_FACTOR]));
  int64_t rep = static_cast<int64_t>(f32(factor * *per_level));
  rep = crew_reputation(rep, group);
  return static_cast<int64_t>(f32(static_cast<double>(rep) * m.factor));
}

int64_t boosted_reputation(const Core& core, const Player& p, int64_t now_us, int64_t rep) {
  return boosted(core, p, now_us, 0, rep, PERK_REPUTATION_BOOST).second;
}

std::optional<std::pair<int64_t, int64_t>> activity_reward(const Tables& t, uint64_t activity, int64_t value,
                                                           int64_t level, Raised* raised) {
  auto a = t.activities.find(activity);
  if (a == t.activities.end()) return std::make_pair(int64_t(0), int64_t(0));
  auto region = t.regions.find(a->second.region);
  auto family = t.families.find(a->second.family);
  if (region == t.regions.end() || family == t.families.end()) return std::make_pair(int64_t(0), int64_t(0));
  const std::vector<int64_t>& th = a->second.thresholds;
  std::optional<int64_t> md;
  for (int64_t i : {3, 2, 1, 0}) {
    size_t k = 0;
    if (!py_index(i, th.size(), &k)) {
      *raised = index_error();
      return std::nullopt;
    }
    if (th[k] <= value) {
      md = i;
      break;
    }
  }
  if (!md) return std::make_pair(int64_t(0), int64_t(0));
  int64_t m = *md;
  if (m == 3 && level < t.max_level()) m = 2;
  const Region& r = region->second;
  const std::vector<double>& fam = family->second;
  if (fam.size() < 2) {
    *raised = index_error();
    return std::nullopt;
  }
  int64_t xp = static_cast<int64_t>(f32(r.xp * fam[0]));
  int64_t bucks = static_cast<int64_t>(f32(r.bucks * fam[1]));
  xp = static_cast<int64_t>(f32(zone_fade(t, r, level) * static_cast<double>(xp)));
  std::optional<double> xm = t.f(ACTIVITY_XP_MEDAL + m, raised);
  if (!xm) return std::nullopt;
  xp = static_cast<int64_t>(f32(static_cast<double>(xp) * *xm));
  std::optional<double> bm = t.f(ACTIVITY_BUCKS_MEDAL + m, raised);
  if (!bm) return std::nullopt;
  bucks = static_cast<int64_t>(f32(static_cast<double>(bucks) * *bm));
  return std::make_pair(xp, bucks);
}

double zone_fade(const Tables& t, const Region& zone, int64_t level) {
  int64_t lv = std::min(level, ZONE_LEVEL_CAP);
  const Region* here = nullptr;
  for (uint64_t key : t.region_order) {
    const Region& z = t.regions.at(key);
    if (z.level <= lv && lv <= z.max) here = &z;
  }
  if (!here) return 1.0;
  if (here->level == zone.max + 1) return 0.5;
  if (zone.max < here->level) return 0.0;
  return 1.0;
}

std::optional<std::pair<int64_t, int64_t>> instant_challenge(const Tables& t, int64_t level, bool speed_trap,
                                                             int64_t waypoints, double distance, Raised* raised) {
  if (t.instant_xp.size() < 2 || t.instant_bucks.size() < 2) return std::make_pair(int64_t(0), int64_t(0));
  int64_t top = t.table.size() > TOP_LEVEL ? t.table[TOP_LEVEL] : 0;
  int64_t lvl = std::min(std::max(level, int64_t(1)), top);
  const size_t st = speed_trap ? 1 : 0;
  double f = speed_trap ? static_cast<double>(std::max<int64_t>(std::min(waypoints, IC_WAYPOINT_CAP) - 1, 0))
                        : f32(f32(std::min(distance, IC_DISTANCE_CAP)) * f32(IC_KM));
  std::optional<double> xs = t.f(XP_BASE + lvl - 1, raised);
  if (!xs) return std::nullopt;
  int64_t xp = static_cast<int64_t>(f32(f32(*xs * t.instant_xp[st]) * f));
  std::optional<double> bs = t.f(BUCKS_BASE + lvl - 1, raised);
  if (!bs) return std::nullopt;
  int64_t bucks = static_cast<int64_t>(f32(f32(*bs * t.instant_bucks[st]) * f));
  return std::make_pair(xp, bucks);
}

std::optional<std::pair<int64_t, int64_t>> summit_skill_reward(const Tables& t, uint64_t activity, int64_t value,
                                                               int64_t level, Raised* raised) {
  const std::pair<int64_t, int64_t> none(0, 0);
  auto a = t.activities.find(activity);
  if (a == t.activities.end()) return none;
  auto family = t.families.find(a->second.family);
  if (family == t.families.end()) return none;
  const std::vector<int64_t>& th = a->second.thresholds;
  std::optional<int64_t> md;
  for (int64_t i : {3, 2, 1, 0}) {
    size_t k = 0;
    if (!py_index(i, th.size(), &k)) {
      *raised = index_error();
      return std::nullopt;
    }
    if (th[k] <= value) {
      md = i;
      break;
    }
  }
  if (!md) return none;
  int64_t m = *md;
  if (m == 3 && level < t.max_level()) m = 2;
  const std::vector<double>& fam = family->second;
  if (fam.size() < 2) {
    *raised = index_error();
    return std::nullopt;
  }
  int64_t top = t.table.size() > TOP_LEVEL ? t.table[TOP_LEVEL] : 0;
  int64_t lvl = std::min(std::max(level, int64_t(1)), top);
  std::optional<double> xs = t.f(XP_BASE + lvl - 1, raised);
  if (!xs) return std::nullopt;
  int64_t xp = static_cast<int64_t>(f32(fam[0] * *xs));
  std::optional<double> bs = t.f(BUCKS_BASE + lvl - 1, raised);
  if (!bs) return std::nullopt;
  int64_t bucks = static_cast<int64_t>(f32(fam[1] * *bs));
  std::optional<double> xm = t.f(ACTIVITY_XP_MEDAL + m, raised);
  if (!xm) return std::nullopt;
  xp = static_cast<int64_t>(f32(static_cast<double>(xp) * *xm));
  std::optional<double> bm = t.f(ACTIVITY_BUCKS_MEDAL + m, raised);
  if (!bm) return std::nullopt;
  bucks = static_cast<int64_t>(f32(static_cast<double>(bucks) * *bm));
  return std::make_pair(xp, bucks);
}

std::optional<uint64_t> table_key(const V& v) {
  if (v.is_bool() || v.is_int()) {
    if (v.is_bool()) return v.as_bool() ? 1 : 0;
    return v.as_u64();
  }
  if (v.is_float()) {
    double f = v.as_float();
    if (f >= 0 && f < 18446744073709551616.0 && std::floor(f) == f) return static_cast<uint64_t>(f);
  }
  return std::nullopt;
}

std::vector<uint64_t> abilities(obj::Py& py, const Tables& t, const V& missions) {
  std::set<int64_t> bits;
  for (const V& mission : py.iter(missions)) {
    if (!obj::hashable(mission)) {
      py.raise("TypeError", std::string("unhashable type: '") + obj::type_name(mission) + "'");
      return {};
    }
    std::optional<uint64_t> key = table_key(mission);
    auto m = key ? t.missions.find(*key) : t.missions.end();
    if (m == t.missions.end()) continue;
    for (const std::string& name : m->second.abilities) {
      auto bit = t.abilities.find(name);
      if (bit != t.abilities.end() && !t.hidden.count(name)) bits.insert(bit->second);
    }
  }
  if (bits.empty()) return {};
  std::vector<uint64_t> words(static_cast<size_t>(*bits.rbegin() / 64 + 1), 0);
  for (int64_t b : bits) words[static_cast<size_t>(b / 64)] |= uint64_t(1) << (b % 64);
  return words;
}

void add_bucks(obj::Py& py, const Tables& t, Player& p, const V& d) {
  if (py.lt(d, obj::I(0))) {
    p.bucks = py.max(obj::I(0), py.add(p.bucks, d));
  } else if (py.gt(d, obj::I(0))) {
    p.bucks = py.min(obj::I(t.max_bucks()), py.add(p.bucks, d));
  }
}

void level_up(obj::Py& py, const Tables& t, Player& p, Random& rng) {
  static const V chances = obj::list_of(std::vector<int64_t>(kLevelUpChance, kLevelUpChance + kLevelUpChances));
  int64_t top = t.table.empty() ? 0 : t.table[TOP_LEVEL];
  while (!py.failed() && py.lt(obj::I(xp_next(t, p.level)), p.xp)) {
    p.xp = py.sub(p.xp, obj::I(xp_next(t, p.level)), true);
    if (p.level < top) p.level += 1;
    V level = py.getitem(p.perks, obj::I(static_cast<int64_t>(LEVEL_UP_PERK)));
    V chance = py.getitem(chances, py.min(level, obj::I(kLevelUpChances - 1)));
    if (py.failed()) return;
    std::optional<int64_t> draw = rng.randrange(100);
    bool second = py.lt(obj::I(draw.value_or(0)), chance);
    p.perk_points += 1 + (second ? 1 : 0);
  }
}

void add_xp(obj::Py& py, const Tables& t, Player& p, const V& d, Random& rng) {
  int64_t top = t.table.empty() ? 0 : t.table[TOP_LEVEL];
  if (py.failed() || obj::eq(d, obj::I(0)) || p.level >= top) return;
  p.xp = py.add(p.xp, d, true);
  level_up(py, t, p, rng);
  if (py.failed()) return;
  p.xp = py.min(p.xp, obj::I(xp_next(t, top)));
}

void apply(obj::Py& py, const Tables& t, Player& p, const V& xp, const V& bucks, Random& rng) {
  add_bucks(py, t, p, bucks);
  add_xp(py, t, p, xp, rng);
}

std::pair<V, V> allow(obj::Py& py, Core& core, Player& p, int64_t now_us, const V& xp_in, const V& bucks_in) {
  V bucks = bucks_in, xp = xp_in;
  if (py.failed()) return {xp, bucks};
  std::map<std::string, std::vector<Earned>>& earned = core.v1_21_state().earned;
  int64_t start = now_us - EARN_WINDOW_US;
  for (auto it = earned.begin(); it != earned.end();) {
    if (it->second.empty() || it->second.back().time <= start) {
      it = earned.erase(it);
    } else {
      ++it;
    }
  }
  std::vector<Earned> book;
  if (!p.path.empty()) {
    auto it = earned.find(p.path);
    if (it != earned.end()) book = it->second;
  } else {
    book = p.earned;
  }
  std::vector<Earned> recent;
  V credited = V::array();
  int64_t got_xp = 0;
  for (const Earned& e : book) {
    if (e.time > start) {
      recent.push_back(e);
      credited.push(e.bucks);
      got_xp += e.xp;
    }
  }
  V got_bucks = py.sum(credited);
  if (py.gt(bucks, obj::I(0))) {
    bucks = py.max(obj::I(0), py.min(bucks, py.sub(obj::I(EARN_WINDOW_BUCKS), got_bucks)));
  }
  if (py.gt(xp, obj::I(0))) xp = py.max(obj::I(0), py.min(xp, obj::I(EARN_WINDOW_XP - got_xp)));
  if (py.failed()) return {xp, bucks};
  bool any = py.gt(bucks, obj::I(0)) || py.gt(xp, obj::I(0));
  if (py.failed()) return {xp, bucks};
  if (any) {
    V credited_xp = py.max(xp, obj::I(0));
    recent.push_back(Earned{now_us, py.max(bucks, obj::I(0)), obj::small_int(credited_xp).value_or(0)});
  }
  if (py.failed()) return {xp, bucks};
  if (!p.path.empty()) {
    if (!recent.empty()) earned[p.path] = std::move(recent);
  } else {
    p.earned = std::move(recent);
  }
  return {xp, bucks};
}

}
}
}
