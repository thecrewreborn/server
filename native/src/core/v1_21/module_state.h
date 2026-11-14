#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"

namespace tcr {
namespace v1_21 {

constexpr uint64_t GROUP_BASE = 0x0000000100000000ull;

struct Earned {
  int64_t time = 0;
  json::Value bucks = json::Value::integer(0);
  int64_t xp = 0;
};

struct ModuleState {
  uint64_t next_group_id = GROUP_BASE + 1;
  std::map<std::string, std::vector<Earned>> earned;
  std::optional<std::string> factions_path;
};

}
}
