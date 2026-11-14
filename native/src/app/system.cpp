#include "app/system.h"

#include <cstdlib>
#include <ctime>

#include "core/util.h"
#include "platform/platform.h"
#include "server/capture.h"
#include "server/core_lock.h"

namespace tcr::app {

std::string console_text(const std::string& text) {
  bool plain = true;
  for (unsigned char c : text) {
    if (c == 0xED) {
      plain = false;
      break;
    }
  }
  if (plain) return text;
  std::string out;
  for (uint32_t cp : py::code_points(text)) {
    if (cp >= 0xD800 && cp <= 0xDFFF) {
      out += '?';
    } else {
      py::append_utf8(out, cp);
    }
  }
  return out;
}

void say(const std::string& text) { plat::print_line(console_text(text)); }
void say_error(const std::string& text) { plat::print_error_line(console_text(text)); }

std::string home_folder() {
  std::string home = plat::env_var("TCREBORN_HOME");
  return home.empty() ? plat::dir_name(plat::executable_path()) : home;
}

std::string config_file(const std::string& home) {
  if (std::getenv("TCREBORN_CONFIG")) return plat::env_var("TCREBORN_CONFIG");
  return plat::join_path(home, "config.ini");
}

std::string first_config_file(const std::string& home) {
  std::string path = plat::env_var("TCREBORN_CONFIG");
  return path.empty() ? plat::join_path(home, "config.ini") : path;
}

double SystemEnv::time() { return fixed_clock_ ? clock_ : plat::wall_time(); }

double SystemEnv::monotonic() { return fixed_clock_ ? clock_ : plat::monotonic(); }

void SystemEnv::urandom(uint8_t* out, size_t n) {
  if (deterministic_) {
    ++counter_;
    Random(counter_).randbytes(out, n);
    return;
  }
  if (!plat::random_bytes(out, n)) {
    say_error("the system's random source failed");
    std::_Exit(1);
  }
}

Random SystemEnv::unseeded_random() {
  if (deterministic_) return Random(0);
  return random_from_urandom(*this);
}

LocalTime SystemEnv::now_local() {
  if (fixed_clock_) return local_time(clock_);
  plat::LocalTime t = plat::local_now();
  LocalTime out;
  out.year = t.year;
  out.month = t.month;
  out.day = t.day;
  out.hour = t.hour;
  out.minute = t.minute;
  out.second = t.second;
  out.microsecond = t.microsecond;
  return out;
}

bool SystemEnv::now_utc(int64_t* sec, int* us) {
  if (fixed_clock_) return split_timestamp(clock_, sec, us);
  int64_t ns = plat::wall_time_ns();
  *sec = ns / 1000000000;
  *us = static_cast<int>(ns % 1000000000 / 1000);
  if (*us < 0) {
    *us += 1000000;
    *sec -= 1;
  }
  return true;
}

LocalTime SystemEnv::local_time(double t) {
  int64_t sec = 0;
  int us = 0;
  if (!split_timestamp(t, &sec, &us)) return LocalTime();
  std::time_t tt = static_cast<std::time_t>(sec);
  std::tm tm{};
#if defined(_WIN32)
  if (localtime_s(&tm, &tt) != 0) return utc_time(t);
#else
  if (!localtime_r(&tt, &tm)) return utc_time(t);
#endif
  LocalTime out;
  out.year = tm.tm_year + 1900;
  out.month = tm.tm_mon + 1;
  out.day = tm.tm_mday;
  out.hour = tm.tm_hour;
  out.minute = tm.tm_min;
  out.second = tm.tm_sec;
  out.microsecond = us;
  return out;
}

std::optional<std::string> SystemEnv::config_stamp() {
  FileStat st;
  OsError err;
  if (!files_.stat(config_path_, &st, &err)) return std::nullopt;
  return std::to_string(st.mtime_ns) + " " + std::to_string(st.size) + " " + std::to_string(st.ino);
}

std::optional<std::string> SystemEnv::config_read() {
  std::string data;
  OsError err;
  if (!files_.read(config_path_, &data, &err)) return std::nullopt;
  return data;
}

void SystemEnv::hash_unlocked(void (*fn)(void*), void* arg) { server::run_hash_unlocked(fn, arg); }

#if defined(TCR_TEST_HOOKS)
void SystemEnv::set_clock(double now) {
  fixed_clock_ = true;
  clock_ = now;
}

void SystemEnv::set_deterministic(uint64_t counter) {
  deterministic_ = true;
  counter_ = counter;
}
#endif

bool SystemRoute::local_ip_for(const std::string& peer, std::string* ip, std::string* error) {
  plat::Error err;
  if (plat::route_ip(peer, 9, ip, &err)) return true;
  *error = plat::python_error_text(err);
  return false;
}

std::string lan_ip() {
  std::string ip;
  plat::Error err;
  if (!plat::route_ip("10.255.255.255", 1, &ip, &err)) return "127.0.0.1";
  return ip;
}

bool SystemRelayHost::append_log(const std::string& path, const std::string& line, std::string* why) {
  return capture::append_log(path, line, why);
}

void SystemRelayHost::log_failed(const std::string& path, const std::string& why) { capture::log_failed(path, why); }

bool SystemRelayHost::local_ip_for(const std::string& peer, std::string* ip, Raised* raised) {
  plat::Error err;
  if (plat::route_ip(peer, 9, ip, &err)) return true;
  *raised = Raised{plat::python_error_name(err), plat::python_error_text(err)};
  return false;
}

}
