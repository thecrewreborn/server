#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.h"

namespace tcr {
class Core;
namespace v1_21 {
namespace scores {

constexpr int64_t U32 = 0xFFFFFFFF;
constexpr uint64_t PVP_KEYS = 5;

struct Entry {
  uint64_t player = 0;
  int64_t score = 0;
  int64_t at = 0;
};
using Boards = std::map<uint64_t, std::vector<Entry>>;

bool lower_better(const Core& core, uint64_t activity);
Boards boards_of(const Core& core, const json::Value& d);
Boards load(Core& core);
std::string dumps(const Boards& boards);
std::vector<Entry> board(Core& core, uint64_t activity);
bool record(Core& core, uint64_t activity, uint64_t player, int64_t score, int64_t at_us);

std::string path(Core& core);

}
}
}
