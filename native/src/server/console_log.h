#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tcr::server::console_log {

constexpr size_t kLines = 5000;
constexpr size_t kLineBytes = 8192;
constexpr size_t kBatch = 2000;

struct Line {
  uint64_t number;
  double time;
  bool error;
  std::string text;
};

void start();
void stop();
void add(const std::string& text, bool error);
std::vector<Line> since(uint64_t after, uint64_t* last);

}
