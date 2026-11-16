#include "core/v1_21/factions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "core/core.h"
#include "core/files.h"
#include "core/util.h"
#include "core/v1_21/module_state.h"
#include "core/v1_21/player.h"

namespace tcr {
namespace v1_21 {
namespace factions {

namespace {

using json::Value;

constexpr int64_t I64_MAX = std::numeric_limits<int64_t>::max();

struct Start {
  int64_t members, total;
  std::array<int64_t, ZONES> zones;
};
const Start START[GROUPS] = {
    {81500, 22940, {5158, 9831, 15281, 19243, 22940}},
    {30422, 7729, {1315, 2576, 4451, 5757, 7729}},
    {47911, 14465, {3021, 5834, 8622, 10857, 14465}},
    {33182, 9269, {1679, 3480, 5448, 7424, 9269}},
    {60719, 16507, {3596, 7107, 9321, 13078, 16507}},
};
const double SCORE_SHARE[6] = {0.56, 0.27, 0.11, 0.035, 0.009, 0.0};
const double POSITION_SHARE[6] = {1.0, 0.001, 0.01, 0.025, 0.05, 0.1};
const int64_t POSITION_CAP[6] = {1, 50, U32, U32, U32, U32};
const int64_t SALARY[7] = {150000, 100000, 60000, 35000, 15000, 6000, 2000};
constexpr double SALARY_BONUS = 0.02;
constexpr double AVERAGE_HIGH = 100000.0, AVERAGE_LOW = 6000.0;
constexpr double SIM_UNIT = 1800.0, SIM_FIRST = 0.6, SIM_SCALE = 1e-6, SIM_SPLIT = 5.0;
const double SIM_LENGTHS[3][3] = {{0.4, 1.0, 200.0}, {0.1, 0.2, 750.0}, {0.03, 0.08, 1500.0}};
const double ZONE_FACTOR[ZONES] = {0.96, 0.98, 1.0, 0.98, 1.0};

double f32(double x) { return static_cast<double>(static_cast<float>(x)); }

struct Date {
  int64_t year, month, day;
};
Date date_of(int64_t now) {
  int64_t z = py::floor_div(now, DAY) + 719468;
  int64_t era = py::floor_div(z, 146097);
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t day = doy - (153 * mp + 2) / 5 + 1;
  int64_t month = mp < 10 ? mp + 3 : mp - 9;
  return Date{yoe + era * 400 + (month <= 2 ? 1 : 0), month, day};
}

int64_t month_days(int64_t year, int64_t month) {
  if (month == 2) return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
  return (month == 4 || month == 6 || month == 9 || month == 11) ? 30 : 31;
}

double lerp32(double a, double b, double t) {
  if ((a <= 0 && 0 <= b) || (b <= 0 && 0 <= a)) {
    double tb = f32(t * b);
    double rest = f32(f32(1.0 - t) * a);
    return f32(tb + rest);
  }
  if (t == 1.0) return b;
  double d = f32(b - a);
  double prod = t * d;
  double x = f32(prod + a);
  return ((t > 1.0) == (b > a)) ? std::max(b, x) : std::min(b, x);
}

bool int_in(const Value* v, int64_t lo, int64_t hi, int64_t* out) {
  if (!v || !v->is_int()) return false;
  std::optional<int64_t> i = v->as_i64();
  if (!i || *i < lo || *i > hi) return false;
  *out = *i;
  return true;
}

template <size_t N>
bool u32s(const Value* v, std::array<int64_t, N>* out) {
  if (!v || !v->is_array() || v->items().size() != N) return false;
  for (size_t k = 0; k < N; k++) {
    if (!int_in(&v->items()[k], 0, U32, &(*out)[k])) return false;
  }
  return true;
}

State read_state(Core& core, int64_t now, bool* is_fresh) {
  *is_fresh = true;
  Files* files = core.files();
  if (files) {
    std::string text, decoded;
    OsError err;
    if (files->read(path(core), &text, &err) && !decode_text_file(files, text, true, &decoded)) {
      std::optional<Value> v = json::loads(py::universal_newlines(decoded), json::kFileMaxDepth);
      if (v) {
        if (std::optional<State> s = valid(*v)) {
          *is_fresh = false;
          return *s;
        }
      }
    }
  }
  return fresh(now);
}

bool write_state(Core& core, const State& s, Raised* raised) {
  Files* files = core.files();
  if (!files) {
    *raised = Raised{"OSError", "no file system"};
    return false;
  }
  const std::string file = path(core);
  FileStat st;
  OsError err;
  if (!files->makedirs(files->dirname(file), &err) || !files->write_atomic(file, dumps(s), false, -1, -1, &st, &err)) {
    *raised = Raised{err.type(), err.text()};
    return false;
  }
  return true;
}

void gain(Faction& f, double elapsed, Random& rng) {
  double t = elapsed > 0 ? f32(elapsed / SIM_UNIT) : 0.0;
  double n = f32(static_cast<double>(f.members));
  double g[ZONES] = {0, 0, 0, 0, 0};
  for (const auto& length : SIM_LENGTHS) {
    for (size_t z = 0; z < ZONES; z++) {
      double r = f32(static_cast<double>(rng.randrange(101).value_or(0)) / 100.0);
      double x = f32(lerp32(f32(length[0]), f32(length[1]), r) * length[2]);
      x = f32(x * f32(ZONE_FACTOR[z]));
      x = f32(x * n);
      x = f32(x * f32(SIM_SCALE));
      x = f32(x / SIM_SPLIT);
      double xt = f32(x * t);
      g[z] = f32(xt + g[z]);
    }
  }
  int64_t run = 0;
  for (size_t z = 0; z < ZONES; z++) {
    run += static_cast<int64_t>(g[z]);
    f.zones[z] = std::min(U32, f.zones[z] + run);
  }
  f.total = std::min(U32, f.total + run);
}

State state_at(Core& core, int64_t now, bool* dirty) {
  bool is_fresh = false;
  State s = read_state(core, now, &is_fresh);
  const State before = s;
  tick(core, s, now);
  *dirty = is_fresh || !(s == before);
  return s;
}

}

int64_t month_index(int64_t now) {
  Date d = date_of(now);
  return d.year * 12 + d.month - 1;
}

bool last_day(int64_t now) {
  Date d = date_of(now);
  return d.day == month_days(d.year, d.month);
}

int64_t month_seconds(int64_t now) { return (date_of(now).day - 1) * DAY + (now - py::floor_div(now, DAY) * DAY); }

State fresh(int64_t now) {
  State s;
  s.month = month_index(now);
  s.last_sim = now;
  s.ticks = 0;
  s.snapshot = false;
  for (size_t z = 0; z < ZONES; z++) {
    for (size_t g = 0; g < GROUPS; g++) s.last_months[z][g] = START[g].zones[z];
  }
  for (size_t g = 0; g < GROUPS; g++) s.factions[g] = Faction{START[g].members, START[g].total, START[g].zones};
  return s;
}

std::optional<State> valid(const Value& v) {
  if (!v.is_object()) return std::nullopt;
  State s;
  int64_t version = 0;
  if (!int_in(v.get("version"), VERSION, VERSION, &version) ||
      !int_in(v.get("month"), std::numeric_limits<int64_t>::min(), I64_MAX, &s.month) ||
      !int_in(v.get("last_sim"), std::numeric_limits<int64_t>::min(), I64_MAX, &s.last_sim) ||
      !int_in(v.get("ticks"), 0, I64_MAX, &s.ticks)) {
    return std::nullopt;
  }
  const Value* snap = v.get("snapshot");
  if (!snap || !snap->is_bool()) return std::nullopt;
  s.snapshot = snap->as_bool();
  const Value* months = v.get("last_months");
  if (!months || !months->is_array() || months->items().size() != ZONES) return std::nullopt;
  for (size_t z = 0; z < ZONES; z++) {
    if (!u32s(&months->items()[z], &s.last_months[z])) return std::nullopt;
  }
  const Value* groups = v.get("factions");
  if (!groups || !groups->is_array() || groups->items().size() != GROUPS) return std::nullopt;
  for (size_t g = 0; g < GROUPS; g++) {
    const Value& f = groups->items()[g];
    if (!f.is_object() || !int_in(f.get("members"), 1, U32, &s.factions[g].members) ||
        !int_in(f.get("total"), 0, U32, &s.factions[g].total) || !u32s(f.get("zones"), &s.factions[g].zones)) {
      return std::nullopt;
    }
  }
  return s;
}

std::string dumps(const State& s) {
  Value months = Value::array();
  for (const auto& z : s.last_months) {
    Value row = Value::array();
    for (int64_t x : z) row.push(Value::integer(x));
    months.push(std::move(row));
  }
  Value groups = Value::array();
  for (const Faction& f : s.factions) {
    Value zones = Value::array();
    for (int64_t x : f.zones) zones.push(Value::integer(x));
    Value rec = Value::object();
    rec.set("members", Value::integer(f.members));
    rec.set("total", Value::integer(f.total));
    rec.set("zones", std::move(zones));
    groups.push(std::move(rec));
  }
  Value out = Value::object();
  out.set("version", Value::integer(VERSION));
  out.set("month", Value::integer(s.month));
  out.set("last_sim", Value::integer(s.last_sim));
  out.set("ticks", Value::integer(s.ticks));
  out.set("snapshot", Value::boolean(s.snapshot));
  out.set("last_months", std::move(months));
  out.set("factions", std::move(groups));
  return json::dumps(out);
}

void tick(Core& core, State& s, int64_t now) {
  const auto keep_last_months = [&s] {
    for (size_t z = 0; z < ZONES; z++) {
      for (size_t g = 0; g < GROUPS; g++) s.last_months[z][g] = s.factions[g].zones[z];
    }
  };
  const int64_t month = month_index(now);
  if (month != s.month) {
    if (!s.snapshot) keep_last_months();
    for (Faction& f : s.factions) {
      f.total = 0;
      f.zones.fill(0);
    }
    s.month = month;
    s.snapshot = last_day(now);
  } else if (last_day(now)) {
    if (!s.snapshot) {
      keep_last_months();
      s.snapshot = true;
    }
  } else {
    s.snapshot = false;
  }
  const int64_t last = s.last_sim;
  if (now < last) {
    s.last_sim = now;
    return;
  }
  const uint64_t diff = static_cast<uint64_t>(now) - static_cast<uint64_t>(last);
  if (diff < static_cast<uint64_t>(SIM_INTERVAL)) return;
  double elapsed = f32(static_cast<double>(diff));
  if (s.ticks == 0) elapsed = f32(elapsed * f32(SIM_FIRST));
  if (month_index(last) != month) elapsed = f32(static_cast<double>(month_seconds(now)));
  if (!last_day(now)) {
    Random rng = core.env().unseeded_random();
    for (Faction& f : s.factions) gain(f, elapsed, rng);
  }
  s.last_sim = now;
  s.ticks = s.ticks == I64_MAX ? I64_MAX : s.ticks + 1;
}

std::string path(Core& core) {
  ModuleState& m = core.v1_21_state();
  if (!m.factions_path) {
    const py::PathRules& paths = py::native_paths();
    config::Result<bool> own = core.config().save_path_configured();
    std::string save = paths.join(core.home(), "save");
    m.factions_path = own.ok() && *own.value ? paths.join(save, "factions.json")
                                             : paths.join(paths.join(save, "1.21"), "factions.json");
  }
  return *m.factions_path;
}

std::optional<State> load(Core& core, int64_t now, Raised* raised) {
  bool dirty = false;
  State s = state_at(core, now, &dirty);
  if (dirty && !write_state(core, s, raised)) return std::nullopt;
  return s;
}

int64_t faction_of(const Player& p) {
  if (!p.group.is_int()) return 0;
  std::optional<int64_t> g = p.group.as_i64();
  return g && *g >= 1 && *g <= static_cast<int64_t>(GROUPS) ? *g : 0;
}

uint64_t joined_us(const Player& p) {
  if (!faction_of(p) || !p.group_joined.is_int()) return 0;
  return p.group_joined.as_u64().value_or(0);
}

int64_t score(const Player& p, int64_t now) {
  if (!faction_of(p) || !p.faction.is_object()) return 0;
  int64_t s = 0, m = 0;
  if (!int_in(p.faction.get("score"), 0, U32, &s) ||
      !int_in(p.faction.get("month"), std::numeric_limits<int64_t>::min(), I64_MAX, &m)) {
    return 0;
  }
  return m == month_index(now) ? s : 0;
}

namespace {

int64_t rep_allow(Core& core, Player& p, int64_t amount, int64_t now) {
  std::map<std::string, std::vector<std::pair<int64_t, int64_t>>>& earned = core.v1_21_state().rep_earned;
  const int64_t start = now - REP_WINDOW_S;
  for (auto it = earned.begin(); it != earned.end();) {
    if (it->second.empty() || it->second.back().first <= start) {
      it = earned.erase(it);
    } else {
      ++it;
    }
  }
  std::vector<std::pair<int64_t, int64_t>> book;
  if (!p.path.empty()) {
    auto it = earned.find(p.path);
    if (it != earned.end()) book = it->second;
  } else {
    book = p.rep_earned;
  }
  std::vector<std::pair<int64_t, int64_t>> recent;
  int64_t got = 0;
  for (const auto& e : book) {
    if (e.first > start) {
      recent.push_back(e);
      got += e.second;
    }
  }
  amount = std::max<int64_t>(0, std::min(amount, REP_WINDOW - got));
  if (amount > 0) recent.emplace_back(now, amount);
  if (!p.path.empty()) {
    if (!recent.empty()) earned[p.path] = std::move(recent);
  } else {
    p.rep_earned = std::move(recent);
  }
  return amount;
}

}

std::optional<int64_t> add_rep(Core& core, Player& p, int64_t zone, int64_t amount, int64_t now, Raised* raised) {
  const int64_t g = faction_of(p);
  if (amount <= 0 || !g) return 0;
  amount = std::min(amount, REPUTATION_CAP);
  bool dirty = false;
  State s = state_at(core, now, &dirty);
  if (last_day(now)) {
    if (dirty && !write_state(core, s, raised)) return std::nullopt;
    return 0;
  }
  amount = rep_allow(core, p, amount, now);
  if (amount <= 0) {
    if (dirty && !write_state(core, s, raised)) return std::nullopt;
    return 0;
  }
  Faction& f = s.factions[static_cast<size_t>(g - 1)];
  f.total = std::min(U32, f.total + amount);
  if (zone >= 0 && zone < static_cast<int64_t>(ZONES)) {
    f.zones[static_cast<size_t>(zone)] = std::min(U32, f.zones[static_cast<size_t>(zone)] + amount);
  }
  if (!write_state(core, s, raised)) return std::nullopt;
  Value now_score = Value::object();
  now_score.set("score", Value::integer(std::min(U32, score(p, now) + amount)));
  now_score.set("month", Value::integer(month_index(now)));
  p.faction = std::move(now_score);
  return amount;
}

std::array<std::pair<int64_t, int64_t>, 6> ranks(const Faction& f) {
  std::array<std::pair<int64_t, int64_t>, 6> out{};
  for (size_t i = 0; i < 6; i++) {
    int64_t need = static_cast<int64_t>(f32(f32(static_cast<double>(f.total)) * f32(SCORE_SHARE[i])));
    double pos = f32(f32(static_cast<double>(f.members)) * f32(POSITION_SHARE[i]));
    pos = std::max(std::min(pos, static_cast<double>(POSITION_CAP[i])), 1.0);
    out[i] = {need, static_cast<int64_t>(pos)};
  }
  return out;
}

int64_t standing(const Faction& f, int64_t s) {
  if (s == 0) return f.members;
  const auto r = ranks(f);
  size_t i = 0;
  while (i < r.size() && !(r[i].first < s)) i++;
  if (i == 0 || i == r.size()) return 0;
  const auto& lo = r[i];
  const auto& up = r[i - 1];
  double t = f32(f32(f32(static_cast<double>(s)) - f32(static_cast<double>(lo.first))) /
                 f32(f32(static_cast<double>(up.first)) - f32(static_cast<double>(lo.first))));
  return static_cast<int64_t>(
      lerp32(f32(static_cast<double>(lo.second)), f32(static_cast<double>(up.second)), t));
}

size_t rank_of(const Faction& f, int64_t s, int64_t stand) {
  if (s == 0) return NO_RANK;
  const auto r = ranks(f);
  for (size_t i = 0; i < r.size(); i++) {
    if (stand < r[i].second) return i;
  }
  return NO_RANK;
}

int64_t salary(size_t rank) {
  int64_t base = SALARY[rank];
  return base + static_cast<int64_t>(f32(f32(static_cast<double>(base)) * f32(SALARY_BONUS)));
}

std::array<int64_t, GROUPS> average_salaries(const State& s) {
  int64_t most = 0;
  for (const Faction& f : s.factions) most = std::max(most, f.members);
  const double top = f32(static_cast<double>(most));
  const double a = AVERAGE_HIGH, b = AVERAGE_LOW;
  std::array<int64_t, GROUPS> out{};
  for (size_t g = 0; g < GROUPS; g++) {
    double t = f32(f32(static_cast<double>(s.factions[g].members)) / top);
    double x = b;
    if (t != 1.0) {
      double prod = t * (b - a);
      x = prod + a;
    }
    out[g] = static_cast<int64_t>(((t > 1.0) == (b > a)) ? std::max(b, x) : std::min(b, x));
  }
  return out;
}

std::vector<int64_t> zone_scores(const State& s) {
  std::vector<int64_t> out;
  for (size_t z = 0; z < ZONES; z++) {
    for (const Faction& f : s.factions) out.push_back(f.zones[z]);
  }
  return out;
}

std::vector<int64_t> last_month_scores(const State& s) {
  std::vector<int64_t> out;
  for (const auto& z : s.last_months) out.insert(out.end(), z.begin(), z.end());
  return out;
}

}
}
}
