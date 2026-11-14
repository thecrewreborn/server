#include "server/console_log.h"

#include <deque>

#include "core/admin.h"
#include "platform/platform.h"

namespace tcr::server::console_log {

namespace {

plat::Mutex& lock() {
  static plat::Mutex m;
  return m;
}
std::deque<Line>& lines() {
  static std::deque<Line> d;
  return d;
}
uint64_t g_last = 0;

void tap(const std::string& text, bool error) { add(text, error); }

}

void start() { plat::set_console_tap(tap); }

void stop() {
  plat::set_console_tap(nullptr);
  plat::Lock l(lock());
  lines().clear();
}

void add(const std::string& text, bool error) {
  double now = plat::wall_time();
  size_t from = 0;
  for (;;) {
    size_t nl = text.find('\n', from);
    size_t len = nl == std::string::npos ? std::string::npos : nl - from;
    std::string part = tcr::admin::valid_utf8(text.substr(from, len), kLineBytes);
    {
      plat::Lock l(lock());
      lines().push_back(Line{++g_last, now, error, std::move(part)});
      while (lines().size() > kLines) lines().pop_front();
    }
    if (nl == std::string::npos) break;
    from = nl + 1;
  }
}

std::vector<Line> since(uint64_t after, uint64_t* last) {
  std::vector<Line> out;
  plat::Lock l(lock());
  *last = g_last;
  for (const Line& line : lines()) {
    if (line.number <= after) continue;
    out.push_back(line);
    if (out.size() >= kBatch) break;
  }
  return out;
}

}
