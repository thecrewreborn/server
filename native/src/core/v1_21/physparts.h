#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/accounts.h"
#include "core/random.h"
#include "core/util.h"
#include "core/v1_21/player.h"

namespace tcr {
class Core;
namespace v1_21 {
namespace physparts {

constexpr int64_t BRONZE = 0, SILVER = 1, GOLD = 2, PLATINUM = 3, CAU_PLATINUM = 4;
constexpr int64_t NO_BONUS = -1, ALL_BONUSES = 4;
constexpr bool REWARD_PARTS = true;
constexpr bool ANNOUNCE_PART = false;
extern const char REWARD_PART[];

struct Mission {
  int64_t slot = 0;
  int64_t level = 0;
  std::vector<int64_t> quality;
  std::vector<double> bonus;
};

struct Table {
  std::vector<std::array<int64_t, 4>> parts;
  std::vector<uint64_t> ids;
  std::map<std::string, Mission> missions;
};

const Table* table();

std::optional<int64_t> find(const Table& t, int64_t slot, int64_t quality, int64_t level, int64_t bonus = NO_BONUS);
bool owned(const Bytes& cfg, std::vector<int64_t>* out, Raised* raised);
std::optional<Bytes> own(const Bytes& cfg, int64_t part, Raised* raised);
std::optional<Bytes> fit(const Bytes& cfg, int64_t part, int64_t slot, Raised* raised);
bool give(Player& p, uint64_t model, uint64_t spec, const std::vector<int64_t>& parts, bool mount, bool* given,
          Raised* raised);
bool has_ability(obj::Py& py, const Player& p, const std::string& name, const Core& core);
bool mission_part(Player& p, uint64_t mission, int64_t medal, uint64_t model, uint64_t spec, Random& rng,
                  const Core& core, std::optional<int64_t>* part, Raised* raised);

}
}
}
