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
      m.type = int_of(v.get("type"));
      m.min_level = int_of(v.get("min_level"));
      m.max_level = int_of(v.get("max_level"));
      const json::Value* rewarding = v.get("rewarding");
      m.rewarding = rewarding && truthy(*rewarding);
      m.factor = float_of(v.get("factor"));
      m.has_thresholds = v.get("thresholds") != nullptr;
      m.thresholds = ints_of(v.get("thresholds"));
      m.reward_values = gamedata::hex_id(str_of(v.get("reward_values")));
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
      r.xp = float_of(e.value.get("xp"));
      r.bucks = float_of(e.value.get("bucks"));
      t->regions[gamedata::hex_id(e.key)] = r;
    }
  }
  if (const json::Value* families = d->get("families")) {
    for (const json::Member& e : families->members()) {
      std::vector<double> f;
      for (const json::Value& x : e.value.items()) f.push_back(float_of(&x));
      t->families[gamedata::hex_id(e.key)] = std::move(f);
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

std::optional<std::pair<int64_t, int64_t>> mission_reward(const Tables& t, uint64_t mission, int64_t score,
                                                          int64_t level, Raised* raised) {
  auto it = t.missions.find(mission);
  if (it == t.missions.end() || !it->second.rewarding) return std::make_pair(int64_t(0), int64_t(0));
  const Mission& m = it->second;
  int64_t kind = m.type;
  if ((kind == 0 || kind == 1 || kind == 5 || kind == 6) && level < m.min_level) {
    return std::make_pair(int64_t(0), int64_t(0));
  }
  auto rv = t.reward_values.find(m.reward_values);
  if (rv == t.reward_values.end()) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(m.reward_values));
    *raised = Raised{"KeyError", buf};
    return std::nullopt;
  }
  if (rv->second.size() < 2) {
    *raised = Raised{"ValueError", "not enough values to unpack (expected 2, got " +
                                       std::to_string(rv->second.size()) + ")"};
    return std::nullopt;
  }
  double xp_factor = pack::f32_of_bits(static_cast<uint32_t>(rv->second[0])),
         bucks_factor = pack::f32_of_bits(static_cast<uint32_t>(rv->second[1]));
  double bucks = 0.0, xp = 0.0;
  int64_t base = 0;
  if (kind == 0 || kind == 1 || kind == 6) {
    base = kind == 6 ? level : m.max_level;
    std::optional<int64_t> md = medal(t, m.thresholds, score, level, raised);
    if (!md) return std::nullopt;
    std::optional<double> b1 = t.f(BUCKS_BASE + base - 1, raised);
    if (!b1) return std::nullopt;
    std::optional<double> b2 = t.f(BUCKS_MEDAL + *md, raised);
    if (!b2) return std::nullopt;
    bucks = f32(f32(bucks_factor * *b1) * *b2);
    std::optional<double> x1 = t.f(XP_BASE + base - 1, raised);
    if (!x1) return std::nullopt;
    std::optional<double> x2 = t.f(XP_MEDAL + *md, raised);
    if (!x2) return std::nullopt;
    xp = f32(f32(xp_factor * *x1) * *x2);
  } else if (kind == 2) {
    base = level;
    std::optional<double> b1 = t.f(BUCKS_BASE + base - 1, raised);
    if (!b1) return std::nullopt;
    bucks = f32(bucks_factor * *b1);
    std::optional<double> x1 = t.f(XP_BASE + base - 1, raised);
    if (!x1) return std::nullopt;
    xp = f32(xp_factor * *x1);
  } else {
    return std::make_pair(int64_t(0), int64_t(0));
  }
  bucks = f32(bucks * m.factor);
  xp = f32(xp * m.factor);
  if ((kind == 0 || kind == 1) && level - base > 10) {
    double fade = f32(f32(static_cast<double>(level - base) - 10.0) / 10.0);
    fade = std::min(std::max(fade, 0.0), 1.0);
    xp = f32(xp * f32(1.0 - fade));
  }
  return std::make_pair(static_cast<int64_t>(xp), static_cast<int64_t>(bucks));
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
  double fade = level <= r.level || level - r.level < 20 ? 1.0 : 0.0;
  const std::vector<double>& fam = family->second;
  if (fam.size() < 2) {
    *raised = index_error();
    return std::nullopt;
  }
  std::optional<double> bm = t.f(ACTIVITY_BUCKS_MEDAL + m, raised);
  if (!bm) return std::nullopt;
  double bucks = f32(f32(r.bucks * fam[1]) * *bm);
  std::optional<double> xm = t.f(ACTIVITY_XP_MEDAL + m, raised);
  if (!xm) return std::nullopt;
  double xp = f32(f32(f32(r.xp * fam[0]) * fade) * *xm);
  return std::make_pair(static_cast<int64_t>(xp), static_cast<int64_t>(bucks));
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
