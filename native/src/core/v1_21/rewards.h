#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/accounts.h"
#include "core/random.h"
#include "core/v1_21/player.h"

namespace tcr {
class Core;
namespace v1_21 {
namespace rewards {

constexpr size_t TOP_LEVEL = 0, MAX_BUCKS = 1, MAX_LEVEL = 3;
constexpr size_t LEVEL_UP_PERK = 7;
constexpr int64_t XP_MEDAL = 0x07, BUCKS_MEDAL = 0x0B, BUCKS_BASE = 0x19, XP_BASE = 0x55, XP_NEXT = 0xCD;
constexpr int64_t ACTIVITY_XP_MEDAL = 0x0F, ACTIVITY_BUCKS_MEDAL = 0x13;

constexpr int64_t EVENT = 1, SUMMIT = 8;
constexpr int64_t EVENT_RANK = 1;
constexpr bool PAY_EVENTS = false;
constexpr bool PAY_SUMMIT = true;
bool player_level_type(int64_t online);

constexpr int64_t EARN_WINDOW_US = 60000000;
constexpr int64_t EARN_WINDOW_BUCKS = 800000, EARN_WINDOW_XP = 100000;

struct Mission {
  int64_t online = 0;
  int64_t min_level = 0;
  int64_t max_level = 0;
  bool rewarding = false;
  double factor = 1.0;
  bool has_thresholds = false;
  std::vector<int64_t> thresholds;
  uint64_t reward_values = 0;
  std::vector<std::string> abilities;
  int64_t cc_gift = 0;
};

struct Activity {
  uint64_t region = 0;
  uint64_t family = 0;
  std::vector<int64_t> thresholds;
};

struct Region {
  int64_t level = 0;
  int64_t max = 0;
  double xp = 0.0;
  double bucks = 0.0;
};

struct Chase {
  bool present = false;
  std::vector<int64_t> chunk_percent;
  std::vector<std::pair<double, double>> crate_values;
  std::vector<std::pair<uint64_t, int64_t>> racer_cars;
  std::vector<std::pair<uint64_t, int64_t>> police_cars;
};

struct Tables {
  std::vector<int64_t> table;
  std::unordered_map<uint64_t, std::vector<int64_t>> reward_values;
  std::unordered_map<uint64_t, Mission> missions;
  std::map<std::string, int64_t> abilities;
  std::set<std::string> hidden;
  std::unordered_map<uint64_t, Activity> activities;
  std::unordered_map<uint64_t, Region> regions;
  std::vector<uint64_t> region_order;
  std::unordered_map<uint64_t, std::vector<double>> families;
  std::map<int64_t, std::vector<double>> perk_boosts;
  Chase chase;
  std::map<int64_t, std::vector<double>> coop;
  std::vector<double> instant_xp, instant_bucks;
  std::set<uint64_t> lower_better;

  std::optional<double> f(int64_t i, Raised* raised) const;
  int64_t max_level() const { return table.size() > MAX_LEVEL ? table[MAX_LEVEL] : 0; }
  int64_t max_bucks() const { return table.size() > MAX_BUCKS ? table[MAX_BUCKS] : 0; }
};

const Tables* tables(const Core& core);

double f32(double x);

std::optional<int64_t> medal(const Tables& t, const std::vector<int64_t>& thresholds, int64_t score, int64_t level,
                             Raised* raised);

constexpr int64_t COOP_RANK = 1;
int64_t coop_bonus(const Tables& t, int64_t amount, int64_t players, int64_t rank = COOP_RANK);
std::optional<std::pair<int64_t, int64_t>> client_reward(const Tables& t, const Mission& m, int64_t score,
                                                         int64_t level, Raised* raised, int64_t group = 1);
std::optional<std::pair<int64_t, int64_t>> mission_reward(const Tables& t, uint64_t mission, int64_t score,
                                                          int64_t level, Raised* raised, int64_t group = 1);
constexpr int64_t PERK_MISSION_BOOST = 4, PERK_REPUTATION_BOOST = 8;
constexpr double PERK_FACTOR = 0.01;
double perk_boost(const Tables* t, const Player& p, int64_t perk);
std::pair<int64_t, int64_t> boosted(const Core& core, const Player& p, int64_t now_us, int64_t xp, int64_t bucks,
                                    std::optional<int64_t> perk = PERK_MISSION_BOOST);

constexpr int64_t FACTION_BONUS = 6;
constexpr bool PAY_BONUS_REPUTATION = false;
constexpr size_t REPUTATION_FACTOR = 2;
constexpr int64_t REPUTATION_BASE = 0x91;
bool faction_type(int64_t online);
constexpr double CREW_REPUTATION_STEP = 0.05;
constexpr int64_t CREW_MAX = 4;
int64_t crew_reputation(int64_t rep, int64_t players, int64_t rank = COOP_RANK);
std::optional<int64_t> mission_reputation(const Tables& t, uint64_t mission, int64_t level, Raised* raised,
                                          int64_t group = 1);
int64_t boosted_reputation(const Core& core, const Player& p, int64_t now_us, int64_t rep);

constexpr int64_t ZONE_LEVEL_CAP = 50;
double zone_fade(const Tables& t, const Region& zone, int64_t level);
std::optional<std::pair<int64_t, int64_t>> activity_reward(const Tables& t, uint64_t activity, int64_t value,
                                                           int64_t level, Raised* raised);
constexpr double IC_DISTANCE_CAP = 20000.0, IC_KM = 0.001;
constexpr int64_t IC_WAYPOINT_CAP = 16;
std::optional<std::pair<int64_t, int64_t>> instant_challenge(const Tables& t, int64_t level, bool speed_trap,
                                                             int64_t waypoints, double distance, Raised* raised);
std::optional<std::pair<int64_t, int64_t>> summit_skill_reward(const Tables& t, uint64_t activity, int64_t value,
                                                               int64_t level, Raised* raised);
std::vector<uint64_t> abilities(obj::Py& py, const Tables& t, const V& missions);
std::optional<uint64_t> table_key(const V& v);

void add_bucks(obj::Py& py, const Tables& t, Player& p, const V& d);
void level_up(obj::Py& py, const Tables& t, Player& p, Random& rng);
void add_xp(obj::Py& py, const Tables& t, Player& p, const V& d, Random& rng);
void apply(obj::Py& py, const Tables& t, Player& p, const V& xp, const V& bucks, Random& rng);

std::pair<V, V> allow(obj::Py& py, Core& core, Player& p, int64_t now_us, const V& xp, const V& bucks);

}
}
}
