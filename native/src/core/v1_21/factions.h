#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/accounts.h"
#include "core/json.h"
#include "core/random.h"

namespace tcr {
class Core;
namespace v1_21 {
struct Player;
namespace factions {

constexpr int VERSION = 2;
constexpr size_t GROUPS = 5, ZONES = 5;
constexpr int64_t NO_ZONE = 0xFF;
constexpr int64_t U32 = 0xFFFFFFFF;
constexpr int64_t REPUTATION_CAP = 12000;
constexpr int64_t REP_WINDOW_S = 60, REP_WINDOW = 36000;
constexpr int64_t NO_STANDING = 0xFFFFFFFF;
constexpr size_t NO_RANK = 6;
constexpr int64_t DAY = 86400;
constexpr int64_t SIM_INTERVAL = 300;

struct Faction {
  int64_t members = 0;
  int64_t total = 0;
  std::array<int64_t, ZONES> zones{};
  bool operator==(const Faction& o) const { return members == o.members && total == o.total && zones == o.zones; }
};

struct State {
  int64_t month = 0;
  int64_t last_sim = 0;
  int64_t ticks = 0;
  bool snapshot = false;
  std::array<std::array<int64_t, GROUPS>, ZONES> last_months{};
  std::array<Faction, GROUPS> factions{};
  bool operator==(const State& o) const {
    return month == o.month && last_sim == o.last_sim && ticks == o.ticks && snapshot == o.snapshot &&
           last_months == o.last_months && factions == o.factions;
  }
};

int64_t month_index(int64_t now);
bool last_day(int64_t now);
int64_t month_seconds(int64_t now);

State fresh(int64_t now);
std::optional<State> valid(const json::Value& v);
std::string dumps(const State& s);
void tick(Core& core, State& s, int64_t now);

std::optional<State> load(Core& core, int64_t now, Raised* raised);

std::string path(Core& core);

int64_t faction_of(const Player& p);
uint64_t joined_us(const Player& p);
int64_t score(const Player& p, int64_t now);
std::optional<int64_t> add_rep(Core& core, Player& p, int64_t zone, int64_t amount, int64_t now, Raised* raised);

std::array<std::pair<int64_t, int64_t>, 6> ranks(const Faction& f);
int64_t standing(const Faction& f, int64_t score);
size_t rank_of(const Faction& f, int64_t score, int64_t stand);
int64_t salary(size_t rank);
std::array<int64_t, GROUPS> average_salaries(const State& s);
std::vector<int64_t> zone_scores(const State& s);
std::vector<int64_t> last_month_scores(const State& s);

}
}
}
