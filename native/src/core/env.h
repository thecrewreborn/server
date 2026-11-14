#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "core/random.h"

namespace tcr {

struct LocalTime {
  int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, microsecond = 0;
};

class Env {
 public:
  virtual ~Env() = default;
  virtual double time() = 0;
  virtual double monotonic() = 0;
  virtual void urandom(uint8_t* out, size_t n) = 0;
  virtual Random unseeded_random() = 0;
  virtual LocalTime local_time(double t);
  virtual LocalTime now_local() { return local_time(time()); }
  virtual bool now_utc(int64_t* sec, int* us);
  virtual void print_line(const std::string& line) { (void)line; }
  virtual bool provides_config() const { return false; }
  virtual std::optional<std::string> config_stamp() { return std::nullopt; }
  virtual std::optional<std::string> config_read() { return std::nullopt; }
  virtual std::optional<std::string> config_path() { return std::nullopt; }
  virtual void hash_unlocked(void (*fn)(void*), void* arg) { fn(arg); }
};

bool split_timestamp(double t, int64_t* sec, int* us);
LocalTime civil_utc(int64_t sec, int us);
LocalTime utc_time(double t);
std::string stamped(Env& env, const std::string& text);

Random random_from_urandom(Env& env);

}
