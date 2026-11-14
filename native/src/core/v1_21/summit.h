#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "core/accounts.h"
#include "core/json.h"
#include "core/random.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/player.h"

namespace tcr {
namespace v1_21 {
namespace summit {

constexpr int64_t NONE = 0xFFFF;
constexpr int64_t NO_KIND = 3;
constexpr int64_t FEE = 1000;
constexpr int64_t EVENT_BUCKS = 3000, EVENT_XP = 800;
constexpr int64_t MONTHLY_BUCKS = 6000, MONTHLY_XP = 1200;
constexpr int64_t POINTS_UNIT = 25, NEXT_THRESHOLD = 325;
constexpr int64_t AUTO_ENTRY_RANK = 2;

std::optional<int64_t> current(int64_t now_us, const gamedata::Summit* data, Raised* raised);
std::optional<int64_t> calendar_time_us(int64_t now_us, const gamedata::Summit* data, Raised* raised);
V* state(obj::Py& py, Player& p);
void sync(obj::Py& py, Player& p, int64_t cur);

bool enter(obj::Py& py, Player& p, int64_t cur);
std::pair<int64_t, int64_t> event_reward(int64_t cur, const gamedata::Summit* data);
std::optional<int64_t> event_slot(uint64_t mission, int64_t cur, const gamedata::Summit* data);
void post_score(obj::Py& py, Player& p, int64_t slot, int64_t score);
void add_points(obj::Py& py, Player& p, int64_t rank, Random& rng);
void result(obj::Py& py, Player& p, int64_t rank, int64_t cur, Random& rng, const gamedata::Summit* data);

}
}
}
