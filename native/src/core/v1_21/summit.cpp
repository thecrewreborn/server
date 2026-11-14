#include "core/v1_21/summit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>

#include "core/env.h"
#include "core/util.h"

namespace tcr {
namespace v1_21 {
namespace summit {

using obj::I;

namespace {

int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool utc_of(int64_t now_us, LocalTime* out, Raised* raised) {
  int64_t sec = 0;
  int us = 0;
  if (!split_timestamp(static_cast<double>(now_us) / 1e6, &sec, &us)) {
    *raised = Raised{"OverflowError", "timestamp out of range for platform time_t"};
    return false;
  }
  *out = civil_utc(sec, us);
  if (out->year < 1 || out->year > 9999) {
    *raised = Raised{"ValueError", "year " + std::to_string(out->year) + " is out of range"};
    return false;
  }
  return true;
}

const int64_t RANK_POINTS[] = {1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3};
constexpr size_t RANK_POINT_COUNT = sizeof(RANK_POINTS) / sizeof(RANK_POINTS[0]);
const int64_t EVENT_SLOTS[] = {5, 6, 8, 9, 10, 11};

}

std::optional<int64_t> current(int64_t now_us, const gamedata::Summit* data, Raised* raised) {
  if (!data || data->records.empty()) return NONE;
  LocalTime n;
  if (!utc_of(now_us, &n, raised)) return std::nullopt;
  auto key = std::make_tuple(int64_t(n.month), int64_t(n.day), int64_t(n.hour));
  int64_t found = NONE;
  for (size_t i = 0; i < data->records.size(); i++) {
    const gamedata::SummitRecord& r = data->records[i];
    int64_t sd = r.start[0], sm = r.start[1], sy = r.start[2], sh = r.start[3];
    int64_t ed = r.end[0], em = r.end[1], eh = r.end[3];
    auto a = std::make_tuple(sm, sd, sh);
    auto b = std::make_tuple(em, ed, eh);
    bool in = a <= b ? (a <= key && key <= b) : (key >= a || key <= b);
    if (n.year >= sy && in) found = static_cast<int64_t>(i);
  }
  return found;
}

std::optional<int64_t> calendar_time_us(int64_t now_us, const gamedata::Summit* data, Raised* raised) {
  if (!data || data->records.empty()) return now_us;
  std::tuple<int64_t, int64_t, int64_t, int64_t> end{INT64_MIN, 0, 0, 0};
  for (const gamedata::SummitRecord& r : data->records) {
    auto e = std::make_tuple(r.end[2], r.end[1], r.end[0], r.end[3]);
    if (e > end) end = e;
  }
  LocalTime n;
  if (!utc_of(now_us, &n, raised)) return std::nullopt;
  auto now = std::make_tuple(int64_t(n.year), int64_t(n.month), int64_t(n.day), int64_t(n.hour), int64_t(n.minute),
                             int64_t(n.second), int64_t(n.microsecond));
  auto end_full = std::make_tuple(std::get<0>(end), std::get<1>(end), std::get<2>(end), std::get<3>(end), int64_t(0),
                                  int64_t(0), int64_t(0));
  if (now < end_full) return now_us;
  int64_t end_year = std::get<0>(end);
  auto nm = std::make_tuple(int64_t(n.month), int64_t(n.day), int64_t(n.hour), int64_t(n.minute));
  auto em = std::make_tuple(std::get<1>(end), std::get<2>(end), std::get<3>(end), int64_t(0));
  int64_t year = nm < em ? end_year : end_year - 1;
  int64_t day = n.month == 2 && n.day == 29 ? 28 : n.day;
  int64_t days = days_from_civil(n.year, n.month, n.day) - days_from_civil(year, n.month, day);
  return now_us - days * 86400 * 1000000;
}

V* state(obj::Py& py, Player& p) {
  if (py.failed()) return nullptr;
  V& s = p.summit;
  if (!s.is_object()) {
    py.raise("AttributeError", std::string("'") + obj::type_name(s) + "' object has no attribute 'setdefault'");
    return nullptr;
  }
  auto setdefault = [&s](const char* key, V v) {
    if (!s.contains(key)) s.set(key, std::move(v));
  };
  setdefault("entered", obj::B(false));
  setdefault("index", I(NONE));
  setdefault("threshold", I(1));
  setdefault("meter", I(0));
  setdefault("last_kind", I(0));
  setdefault("tokens", obj::list_of({0, 0, 0}));
  setdefault("next_kind", I(NO_KIND));
  setdefault("rank", I(0));
  setdefault("results", I(0));
  setdefault("value", I(0));
  setdefault("scores", V::object());
  return &s;
}

void sync(obj::Py& py, Player& p, int64_t cur) {
  V* s = state(py, p);
  if (!s) return;
  if (!obj::eq(*s->get("index"), I(cur)) || cur == NONE) {
    s->set("entered", obj::B(false));
    s->set("index", I(NONE));
    s->set("scores", V::object());
  } else if (!obj::truthy(*s->get("entered"))) {
    s->set("entered", obj::B(true));
  }
}

bool enter(obj::Py& py, Player& p, int64_t cur) {
  V* s = state(py, p);
  if (!s) return false;
  if (obj::truthy(*s->get("entered")) && obj::eq(*s->get("index"), I(cur))) return true;
  bool short_of = py.lt(p.bucks, I(FEE));
  if (py.failed() || short_of || cur == NONE) return false;
  p.bucks = py.sub(p.bucks, I(FEE), true);
  if (py.failed()) return false;
  s->set("entered", obj::B(true));
  s->set("index", I(cur));
  return true;
}

std::pair<int64_t, int64_t> event_reward(int64_t cur, const gamedata::Summit* data) {
  bool monthly = data && !data->records.empty() && cur != NONE && data->records[static_cast<size_t>(cur)].monthly;
  return monthly ? std::make_pair(MONTHLY_XP, MONTHLY_BUCKS) : std::make_pair(EVENT_XP, EVENT_BUCKS);
}

std::optional<int64_t> event_slot(uint64_t mission, int64_t cur, const gamedata::Summit* data) {
  if (!data || data->records.empty() || cur == NONE) return std::nullopt;
  char name[32];
  std::snprintf(name, sizeof(name), "0x%016llx", static_cast<unsigned long long>(mission));
  const std::vector<std::string>& events = data->records[static_cast<size_t>(cur)].events;
  for (size_t k = 0; k < events.size(); k++) {
    if (events[k] == name) return k < 6 ? std::optional<int64_t>(EVENT_SLOTS[k]) : std::nullopt;
  }
  return std::nullopt;
}

void post_score(obj::Py& py, Player& p, int64_t slot, int64_t score) {
  V* s = state(py, p);
  if (!s) return;
  V& scores = *s->get("scores");
  if (!scores.is_object()) {
    py.raise("AttributeError", std::string("'") + obj::type_name(scores) + "' object has no attribute 'get'");
    return;
  }
  const V key = V::string(py::int_to_text(slot));
  const V* have = scores.get(key.as_string());
  bool better = py.gt(I(score), have ? *have : I(0));
  if (py.failed() || !better) return;
  py.setitem(scores, key, I(score));
}

void add_points(obj::Py& py, Player& p, int64_t rank, Random& rng) {
  V* s = state(py, p);
  if (!s) return;
  int64_t gain = RANK_POINTS[std::min<int64_t>(rank, RANK_POINT_COUNT - 1)];
  V total = py.add(I(gain), *s->get("meter"));
  bool below = py.lt(total, *s->get("threshold"));
  if (py.failed()) return;
  if (below) {
    s->set("meter", std::move(total));
    return;
  }
  s->set("meter", I(0));
  s->set("threshold", I(NEXT_THRESHOLD / POINTS_UNIT / 10));
  V kind = *s->get("next_kind");
  s->set("last_kind", kind);
  int64_t pick = rng.randrange(3).value_or(0);
  if (obj::eq(I(pick), kind)) pick = pick < 2 ? pick + 1 : 0;
  s->set("next_kind", I(pick));
  bool in = false;
  for (int64_t k = 0; k < 3; k++) in = in || obj::same(I(k), kind);
  if (!in) return;
  if (kind.is_float()) {
    py.raise("TypeError", "tuple indices must be integers or slices, not float");
    return;
  }
  int64_t at = (obj::small_int(kind).value_or(0) + 1) % 3;
  V& tokens = *s->get("tokens");
  V grown = py.add(py.getitem(tokens, I(at)), I(1), true);
  py.setitem(tokens, I(at), std::move(grown));
}

void result(obj::Py& py, Player& p, int64_t rank, int64_t cur, Random& rng, const gamedata::Summit* data) {
  V* s = state(py, p);
  if (!s) return;
  s->set("rank", I(rank));
  add_points(py, p, rank, rng);
  if (py.failed()) return;
  if (!obj::truthy(*s->get("entered")) && cur != NONE && rank + 1 >= AUTO_ENTRY_RANK + 1) {
    bool monthly = data && !data->records.empty() ? data->records[static_cast<size_t>(cur)].monthly : false;
    if (!monthly) {
      s->set("entered", obj::B(true));
      s->set("index", I(cur));
    }
  }
}

}
}
}
