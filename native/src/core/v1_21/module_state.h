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

struct InstantEnd {
  std::string name, key;
  bool speed_trap = false;
  int64_t waypoints = 0;
  double distance = 0.0;
  json::Value earned_bucks, earned_xp;
};

struct ModuleState {
  uint64_t next_group_id = GROUP_BASE + 1;
  std::map<std::string, std::vector<Earned>> earned;
  std::map<std::string, std::vector<std::pair<int64_t, int64_t>>> rep_earned;
  std::optional<std::string> factions_path;
  std::optional<std::string> scores_path;
  std::vector<InstantEnd> pending_instant;
  std::vector<std::pair<std::string, int64_t>> pending_actions;
  std::optional<std::string> club_saves;
};

}
}
