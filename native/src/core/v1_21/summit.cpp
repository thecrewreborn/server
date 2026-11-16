#include "core/v1_21/summit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>

#include "core/env.h"
#include "core/util.h"
#include "core/v1_21/cars.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/physparts.h"

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

std::optional<int64_t> occurrence(int64_t index, int64_t now_us, const gamedata::Summit& data, Raised* raised) {
  const gamedata::SummitRecord& r = data.records[static_cast<size_t>(index)];
  int64_t sd = r.start[0], sm = r.start[1], sy = r.start[2], sh = r.start[3];
  LocalTime n;
  if (!utc_of(now_us, &n, raised)) return std::nullopt;
  int64_t year = std::make_tuple(int64_t(n.month), int64_t(n.day), int64_t(n.hour)) < std::make_tuple(sm, sd, sh)
                     ? n.year - 1
                     : n.year;
  return std::max(year, sy);
}

namespace {

std::optional<int64_t> int_of(const V* v) {
  if (!v || !v->is_int()) return std::nullopt;
  return v->as_i64();
}

}

std::optional<std::pair<uint64_t, uint64_t>> car_of(const V* v) {
  if (!v || !v->is_array() || v->items().size() != 2) return std::nullopt;
  uint64_t out[2];
  for (size_t i = 0; i < 2; i++) {
    const V& x = v->items()[i];
    if (!x.is_int()) return std::nullopt;
    std::optional<uint64_t> u = x.as_u64();
    if (!u) return std::nullopt;
    out[i] = *u;
  }
  return std::make_pair(out[0], out[1]);
}

std::optional<int64_t> entry_year(const V& s, std::optional<int64_t> idx) {
  const V* v = s.is_object() ? s.get("occurrence") : nullptr;
  if (!idx || !v || !v->is_array() || v->items().size() != 2) return std::nullopt;
  std::optional<int64_t> rec = int_of(&v->items()[0]), year = int_of(&v->items()[1]);
  if (!rec || *rec != *idx || !year) return std::nullopt;
  return year;
}

void sync(obj::Py& py, Player& p, int64_t cur, int64_t now_us, const gamedata::Summit* data) {
  V* s = state(py, p);
  if (!s) return;
  const std::optional<int64_t> idx = int_of(s->get("index"));
  const std::optional<int64_t> stored = entry_year(*s, idx);
  bool stale = false;
  if (cur != NONE && idx && *idx == cur && stored && data && !data->records.empty() && cur >= 0 &&
      cur < static_cast<int64_t>(data->records.size())) {
    Raised r;
    std::optional<int64_t> now_year = occurrence(cur, now_us, *data, &r);
    if (!now_year) {
      py.raise(r);
      return;
    }
    stale = *stored != *now_year;
  }
  if (!obj::eq(*s->get("index"), I(cur)) || cur == NONE || stale) {
    if (obj::truthy(*s->get("entered")) && data && !data->records.empty() && idx && (*idx != cur || stale) &&
        *idx >= 0 && *idx < static_cast<int64_t>(data->records.size())) {
      std::optional<int64_t> year = stored;
      if (!year) {
        Raised r;
        year = occurrence(*idx, now_us, *data, &r);
        if (!year) {
          py.raise(r);
          return;
        }
      }
      const V* flag = s->get("played");
      const bool played = (flag && flag->is_bool() && flag->as_bool()) || obj::truthy(*s->get("scores"));
      std::optional<std::pair<uint64_t, uint64_t>> car = car_of(s->get("car"));
      V finished = obj::list_of({*idx, *year});
      finished.push(obj::B(played));
      finished.push(car ? obj::list_of_u64({car->first, car->second}) : V::null());
      s->set("finished", std::move(finished));
    }
    s->set("entered", obj::B(false));
    s->set("index", I(NONE));
    s->set("scores", V::object());
    s->erase("occurrence");
    s->erase("played");
    s->erase("car");
  } else if (!obj::truthy(*s->get("entered"))) {
    s->set("entered", obj::B(true));
  }
}

std::optional<Finished> take_finished(V& s, const gamedata::Summit* data) {
  if (!s.is_object()) return std::nullopt;
  const V* got = s.get("finished");
  if (!got) return std::nullopt;
  V v = *got;
  s.erase("finished");
  if (!v.is_array() || (v.items().size() != 2 && v.items().size() != 4)) return std::nullopt;
  std::optional<int64_t> idx = int_of(&v.items()[0]), year = int_of(&v.items()[1]);
  if (!idx || !year) return std::nullopt;
  if (!data || data->records.empty() || *idx < 0 || *idx >= static_cast<int64_t>(data->records.size())) {
    return std::nullopt;
  }
  Finished f;
  f.index = *idx;
  f.year = *year;
  if (v.items().size() == 4) {
    const V& played = v.items()[2];
    f.played = played.is_bool() && played.as_bool();
    f.car = car_of(&v.items()[3]);
  }
  return f;
}

namespace {

bool entered_at(obj::Py& py, V& s, int64_t cur, int64_t now_us, const gamedata::Summit* data) {
  s.set("entered", obj::B(true));
  s.set("index", I(cur));
  if (!data || cur < 0 || cur >= static_cast<int64_t>(data->records.size())) {
    py.raise("TypeError", "'NoneType' object is not subscriptable");
    return false;
  }
  Raised r;
  std::optional<int64_t> year = occurrence(cur, now_us, *data, &r);
  if (!year) {
    py.raise(r);
    return false;
  }
  s.set("occurrence", obj::list_of({cur, *year}));
  return true;
}

}

bool enter(obj::Py& py, Player& p, int64_t cur, int64_t now_us, const gamedata::Summit* data) {
  V* s = state(py, p);
  if (!s) return false;
  if (obj::truthy(*s->get("entered")) && obj::eq(*s->get("index"), I(cur))) return true;
  bool short_of = py.lt(p.bucks, I(FEE));
  if (py.failed() || short_of || cur == NONE) return false;
  p.bucks = py.sub(p.bucks, I(FEE), true);
  if (py.failed()) return false;
  return entered_at(py, *s, cur, now_us, data);
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

int64_t esk_points(int64_t rank) {
  return RANK_POINTS[std::min<int64_t>(rank, RANK_POINT_COUNT - 1)] * POINTS_UNIT * 10;
}

void give_tokens(obj::Py& py, V& s, int64_t count) {
  int64_t kind = last_won(s);
  for (int64_t k = 0; k < count && !py.failed(); k++) {
    kind = kind == NO_WON ? 0 : (kind + 1) % 3;
    V& tokens = *s.get("tokens");
    V v = py.getitem(tokens, I(kind));
    if (py.failed()) return;
    if (!obj::eq(v, I(0xFF))) {
      V grown = py.add(v, I(1));
      if (py.failed()) return;
      py.setitem(tokens, I(kind), std::move(grown));
      if (py.failed()) return;
    }
    s.set("last_won", I(kind));
  }
}

void add_esk(obj::Py& py, Player& p, int64_t points, const gamedata::Summit* data) {
  if (!data || data->esk_by_level.empty()) return;
  V* s = state(py, p);
  if (!s) return;
  std::optional<int64_t> lv = int_of(s->get("threshold")), x = int_of(s->get("meter"));
  int64_t level = lv && *lv >= 0 && *lv <= 0xFF ? *lv : 1;
  int64_t xp = ((x && *x >= 0 && *x <= 0xFFFF ? *x : 0) + points) & 0xFFFF;
  const std::vector<int64_t>& table = data->esk_by_level;
  auto step = [&table](int64_t l) -> int64_t {
    int64_t top = std::min<int64_t>(static_cast<int64_t>(table.size()), ESK_LEVEL_CAP);
    return l >= 1 && l <= top ? table[static_cast<size_t>(l - 1)] : 0;
  };
  int64_t need = step(level), gained = 0;
  while (xp >= need) {
    if (level < ESK_LEVEL_CAP) level++;
    xp -= need;
    need = step(level);
    gained++;
    if (need == 0 && level >= ESK_LEVEL_CAP) break;
  }
  s->set("threshold", I(level));
  s->set("meter", I(xp));
  if (gained) give_tokens(py, *s, gained);
}

void result(obj::Py& py, Player& p, int64_t rank, int64_t cur, int64_t now_us, const gamedata::Summit* data) {
  V* s = state(py, p);
  if (!s) return;
  s->set("rank", I(rank));
  add_esk(py, p, esk_points(rank), data);
  if (py.failed()) return;
  s = state(py, p);
  if (!obj::truthy(*s->get("entered")) && cur != NONE && rank + 1 >= AUTO_ENTRY_RANK + 1) {
    bool monthly = data && !data->records.empty() ? data->records[static_cast<size_t>(cur)].monthly : false;
    if (!monthly) entered_at(py, *s, cur, now_us, data);
  }
}

int64_t last_won(const V& s) {
  std::optional<int64_t> v = s.is_object() ? int_of(s.get("last_won")) : std::nullopt;
  return v && *v >= 0 && *v <= 2 ? *v : NO_WON;
}

bool esk(obj::Py& py, Player& p, Bytes* out, Raised* raised) {
  V* s = state(py, p);
  if (!s) {
    *raised = py.raised();
    return false;
  }
  pack::Packer pk;
  pk.B(*s->get("threshold")).H(*s->get("meter"));
  if (pk.failed()) {
    *raised = pk.raised();
    return false;
  }
  const V& tokens = *s->get("tokens");
  if (!tokens.is_array() && !tokens.is_object() && !tokens.is_string()) {
    *raised = Raised{"TypeError", std::string("Value after * must be an iterable, not ") + obj::type_name(tokens)};
    return false;
  }
  std::vector<V> items = py.iter(tokens);
  if (items.size() != 3) {
    *raised = pack::error("pack expected 4 items for packing (got " + py::uint_to_text(items.size() + 1) + ")");
    return false;
  }
  for (const V& x : items) pk.B(x);
  pk.B(last_won(*s));
  if (pk.failed()) {
    *raised = pk.raised();
    return false;
  }
  out->insert(out->end(), pk.out().begin(), pk.out().end());
  return true;
}

int64_t boost(const Player& p, int64_t now_us) {
  if (!p.summit.is_object()) return NO_BOOST;
  std::optional<int64_t> t = int_of(p.summit.get("boost_type")), end = int_of(p.summit.get("boost_expiry_us"));
  return t && *t >= 0 && *t <= 2 && end && *end >= now_us ? *t : NO_BOOST;
}

std::optional<double> boost_factor(const Player& p, int64_t now_us, const gamedata::Summit* data) {
  int64_t t = boost(p, now_us);
  if (t == NO_BOOST || !data || static_cast<size_t>(t) >= data->wheel.size()) return std::nullopt;
  return data->wheel[static_cast<size_t>(t)].factor;
}

std::pair<int64_t, int64_t> boost_wire(const Player& p, int64_t now_us, int64_t client_now_us) {
  int64_t t = boost(p, now_us);
  if (t == NO_BOOST) return {NO_BOOST, 0};
  return {t, *int_of(p.summit.get("boost_expiry_us")) + (client_now_us - now_us)};
}

int64_t medal(int64_t level, const gamedata::Summit* data) {
  if (!data || !data->medals.present) return NO_MEDAL;
  const gamedata::SummitMedals& m = data->medals;
  if (PLACE_PERCENT <= m.platinum && level >= m.platinum_level) return PLATINUM;
  if (PLACE_PERCENT <= m.gold) return GOLD;
  if (PLACE_PERCENT <= m.silver) return SILVER;
  if (PLACE_PERCENT <= m.bronze) return BRONZE;
  return NO_MEDAL;
}

namespace {

int64_t pack_quality(int64_t medal) {
  return medal <= BRONZE ? physparts::BRONZE : medal == SILVER ? physparts::SILVER
                                           : medal == GOLD     ? physparts::GOLD
                                                               : physparts::PLATINUM;
}

void crew_credits(Player& p, int64_t amount) {
  if (!p.cc_fixed) p.crew_credits += amount;
}

}

std::optional<int64_t> spin(obj::Py& py, const Core& core, Player& p, int64_t cur, int64_t year, int64_t now_us,
                            Random& rng, const gamedata::Summit* data,
                            const std::optional<std::pair<uint64_t, uint64_t>>& car) {
  if (!data || data->wheel.empty() || cur == NONE) return std::nullopt;
  V* s = state(py, p);
  if (!s) return std::nullopt;
  const V occ = obj::list_of({cur, year});
  const V* had = s->get("wheel_occurrence");
  const V* rolled = s->get("wheel_index");
  if ((had && obj::eq(*had, occ)) || (!had && rolled && obj::eq(*rolled, I(cur)))) return std::nullopt;
  const gamedata::SummitRecord& record = data->records[static_cast<size_t>(cur)];
  int64_t t = record.type;
  if (t < 0 || static_cast<size_t>(t) >= data->wheel.size()) return std::nullopt;
  const gamedata::SummitWheel& w = data->wheel[static_cast<size_t>(t)];
  const int64_t md = medal(p.level, data);
  size_t m = static_cast<size_t>(md);
  if (m >= w.chances.size()) return std::nullopt;
  const std::vector<int64_t>& chances = w.chances[m];
  int64_t total = 0;
  for (int64_t c : chances) total += c;
  if (total <= 0) return std::nullopt;
  int64_t pick = rng.randrange(total).value_or(0);
  size_t kind = 0;
  while (kind < chances.size() && pick >= chances[kind]) {
    pick -= chances[kind];
    kind++;
  }
  s->set("wheel_index", I(cur));
  s->set("wheel_occurrence", occ);
  s->set("wheel_won", I(static_cast<int64_t>(kind)));
  const int64_t k = static_cast<int64_t>(kind);
  if (k == BOOST) {
    const int64_t end = now_us + w.days * DAY_US;
    const int64_t running = boost(p, now_us);
    bool take = running == NO_BOOST || static_cast<size_t>(running) >= data->wheel.size();
    if (!take) {
      const double old = data->wheel[static_cast<size_t>(running)].factor;
      take = w.factor > old || (w.factor == old && end > *int_of(s->get("boost_expiry_us")));
    }
    if (take) {
      s->set("boost_type", I(t));
      s->set("boost_expiry_us", I(end));
    }
  } else if (k == CREW_CREDITS) {
    crew_credits(p, w.crew_credits);
  } else if (k == SUMMIT_CAR) {
    bool granted = false;
    if (record.car != NO_ID) {
      Raised r;
      std::optional<bool> got = cars::grant(core, p, record.car, &r);
      if (!got) {
        py.raise(r);
        return std::nullopt;
      }
      granted = *got;
    }
    if (!granted) crew_credits(p, w.crew_credits);
  } else if (k == PARTS_PACK) {
    bool given = false;
    if (car) {
      Raised r;
      std::optional<std::vector<int64_t>> parts;
      if (!physparts::pack_parts(p, car->first, car->second, pack_quality(md), p.level, rng, &parts, &r)) {
        py.raise(r);
        return std::nullopt;
      }
      given = parts.has_value();
    }
    if (!given) crew_credits(p, w.crew_credits);
  } else if (k == ESK_TOKEN) {
    give_tokens(py, *state(py, p), 1);
    if (py.failed()) return std::nullopt;
  }
  return k;
}

}
}
}
