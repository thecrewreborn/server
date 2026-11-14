#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/logtext.h"

namespace tcr::http {

bool status_text(int code, const char** phrase, const char** explain);

std::string html_escape(std::string_view s);

std::string http_date(double t);

struct UtcTime {
  int year, month, day, hour, minute, second, weekday;
};
UtcTime utc_time(double t);

bool py_int_latin1(std::string_view s, int64_t* out);

class Headers {
 public:
  static Headers parse(std::string_view block);
  std::optional<std::string> get(std::string_view name) const;
  const std::vector<std::pair<std::string, std::string>>& items() const { return items_; }

 private:
  std::vector<std::pair<std::string, std::string>> items_;
};

struct Version {
  bool valid = false;
  uint64_t major = 0, minor = 0;
  std::string base;
};
Version parse_version(std::string_view word);

}
