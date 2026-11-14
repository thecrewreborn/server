#include "server/capture.h"

#include <atomic>
#include <cstdio>
#include <vector>

#include "platform/platform.h"
#include "core/logtext.h"

namespace tcr::capture {

namespace {

std::atomic<long long> g_rotate_bytes{kRotateBytes};
plat::Mutex g_warn_lock;
double g_last_warned = -1e9;

}

long long rotate_bytes() { return g_rotate_bytes.load(); }
void set_rotate_bytes(long long n) { g_rotate_bytes.store(n); }

bool append_log(const std::string& path, std::string_view line, std::string* why) {
  std::string text;
  text.reserve(line.size() + 1);
  for (char c : line) {
    if (c == '\n') {
      text += plat::kTextNewline;
    } else {
      text += c;
    }
  }
  plat::Error err;
  bool full = false;
  {
    plat::AppendFile f;
    if (!f.open(path, &err)) {
      if (why) *why = plat::python_error_text(err) + ": " + tcr::repr_utf8(path);
      return false;
    }
    long long size = f.size(&err);
    if (size < 0) {
      if (why) *why = plat::python_error_text(err);
      return false;
    }
    full = size >= rotate_bytes();
    if (!full && !f.write(text.data(), text.size(), &err)) {
      if (why) *why = plat::python_error_text(err);
      return false;
    }
  }
  if (full) {
    std::string old = path + ".1";
    if (!plat::replace_file(path, old, &err)) {
      if (why) *why = plat::python_error_text(err) + ": " + tcr::repr_utf8(path) + " -> " + tcr::repr_utf8(old);
      return false;
    }
    plat::AppendFile f;
    if (!f.open(path, &err)) {
      if (why) *why = plat::python_error_text(err) + ": " + tcr::repr_utf8(path);
      return false;
    }
    if (!f.write(text.data(), text.size(), &err)) {
      if (why) *why = plat::python_error_text(err);
      return false;
    }
  }
  return true;
}

void log_failed(const std::string& path, const std::string& why) {
  {
    plat::Lock lock(g_warn_lock);
    double now = plat::monotonic();
    if (now - g_last_warned < 60) return;
    g_last_warned = now;
  }
  console("cannot write " + path + ": " + why +
          " (the server goes on without this log; this message repeats at most once a minute)");
}

void write_line(const std::string& path, std::string_view line) {
  std::string why;
  if (!append_log(path, line, &why)) log_failed(path, why);
}

std::string iso_now() {
  plat::LocalTime t = plat::local_now();
  char buf[64];
  if (t.microsecond) {
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06d", t.year, t.month, t.day, t.hour, t.minute,
                  t.second, t.microsecond);
  } else {
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute,
                  t.second);
  }
  return buf;
}

std::string clock_now() {
  plat::LocalTime t = plat::local_now();
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hour, t.minute, t.second);
  return buf;
}

void console(const std::string& text) { plat::print_line("[" + clock_now() + "] " + text); }

std::string request_line(const std::string& client_ip, std::string_view method, std::string_view path,
                         std::string_view auth, std::string_view raw, int status, std::string_view reply_json) {
  std::string logged_path = log_path(path).substr(0, kLogPath);
  std::string line = "{\"time\": " + json_quote_utf8(iso_now()) + ", \"client\": " + json_quote_utf8(client_ip) +
                     ", \"method\": " + json_quote_latin1(method) + ", \"path\": " +
                     json_quote_latin1(logged_path) + ", \"auth\": " + json_quote_latin1(redact(auth)) +
                     ", \"body\": " + json_quote_utf8(log_body(path, raw)) + ", \"status\": " +
                     std::to_string(status) +
                     ", \"reply\": " + std::string(reply_json) + "}\n";
  return line;
}

}
