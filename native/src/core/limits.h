#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "core/ordered_map.h"

namespace tcr {

constexpr int kLinesPerMin = 20;

class RateLimit {
 public:
  RateLimit(int64_t count, double window, size_t keys = 65536) : count_(count), window_(window), keys_(keys) {}
  bool allow(const std::string& key, double now);

 private:
  int64_t count_;
  double window_;
  size_t keys_;
  OrderedMap<std::deque<double>> times_;
};

class LineLimit {
 public:
  explicit LineLimit(std::string label, int64_t per_window = kLinesPerMin, double window = 60.0)
      : label_(std::move(label)), per_window_(per_window), window_(window) {}
  bool allow(const std::string& key, double now, std::string* suppressed);

 private:
  std::string label_;
  int64_t per_window_;
  double window_;
  OrderedMap<double> said_;
  double start_ = -1e9;
  int64_t count_ = 0;
  int64_t left_out_ = 0;
};

}
