#include "core/env.h"

#include <cmath>
#include <cstdio>

namespace tcr {

bool split_timestamp(double t, int64_t* sec, int* us) {
  if (!std::isfinite(t)) return false;
  double intpart = 0.0;
  double floatpart = std::modf(t, &intpart);
  floatpart *= 1e6;
  double rounded = std::round(floatpart);
  if (std::fabs(floatpart - rounded) == 0.5) rounded = 2.0 * std::round(floatpart / 2.0);
  floatpart = rounded;
  if (floatpart >= 1e6) {
    floatpart -= 1e6;
    intpart += 1.0;
  } else if (floatpart < 0) {
    floatpart += 1e6;
    intpart -= 1.0;
  }
  if (!(intpart >= -9.2e18 && intpart <= 9.2e18)) return false;
  *sec = static_cast<int64_t>(intpart);
  *us = static_cast<int>(floatpart);
  return true;
}

LocalTime civil_utc(int64_t sec, int us) {
  int64_t days = sec / 86400;
  int64_t rem = sec % 86400;
  if (rem < 0) {
    rem += 86400;
    days -= 1;
  }
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t d = doy - (153 * mp + 2) / 5 + 1;
  int64_t m = mp < 10 ? mp + 3 : mp - 9;
  LocalTime out;
  out.year = static_cast<int>(y + (m <= 2 ? 1 : 0));
  out.month = static_cast<int>(m);
  out.day = static_cast<int>(d);
  out.hour = static_cast<int>(rem / 3600);
  out.minute = static_cast<int>(rem % 3600 / 60);
  out.second = static_cast<int>(rem % 60);
  out.microsecond = us;
  return out;
}

LocalTime utc_time(double t) {
  int64_t sec = 0;
  int us = 0;
  if (!split_timestamp(t, &sec, &us)) return LocalTime();
  return civil_utc(sec, us);
}

LocalTime Env::local_time(double t) { return utc_time(t); }

bool Env::now_utc(int64_t* sec, int* us) { return split_timestamp(time(), sec, us); }

std::string stamped(Env& env, const std::string& text) {
  LocalTime t = env.now_local();
  char buf[32];
  std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] ", t.hour, t.minute, t.second);
  return buf + text;
}

}
