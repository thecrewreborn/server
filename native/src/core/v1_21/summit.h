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
class Core;
namespace v1_21 {
namespace summit {

constexpr int64_t NONE = 0xFFFF;
constexpr int64_t NO_KIND = 3;
constexpr int64_t FEE = 1000;
constexpr int64_t POINTS_UNIT = 25, NEXT_THRESHOLD = 325;
constexpr int64_t AUTO_ENTRY_RANK = 2;
constexpr int64_t NO_WON = 3;
constexpr int64_t NO_BOOST = 3;
constexpr int64_t DAY_US = 86400000000;
constexpr int64_t SUMMIT_CAR = 0, PARTS_PACK = 1, ESK_TOKEN = 2, CREW_CREDITS = 3, BOOST = 4, NOTHING = 5;
constexpr int64_t NO_MEDAL = 0, BRONZE = 1, SILVER = 2, GOLD = 3, PLATINUM = 4;
constexpr int64_t PLACE_PERCENT = 0;

std::optional<int64_t> current(int64_t now_us, const gamedata::Summit* data, Raised* raised);
std::optional<int64_t> calendar_time_us(int64_t now_us, const gamedata::Summit* data, Raised* raised);
V* state(obj::Py& py, Player& p);
std::optional<int64_t> occurrence(int64_t index, int64_t now_us, const gamedata::Summit& data, Raised* raised);
std::optional<int64_t> entry_year(const V& s, std::optional<int64_t> idx);
void sync(obj::Py& py, Player& p, int64_t cur, int64_t now_us, const gamedata::Summit* data);
std::optional<std::pair<uint64_t, uint64_t>> car_of(const V* v);
struct Finished {
  int64_t index = 0, year = 0;
  bool played = false;
  std::optional<std::pair<uint64_t, uint64_t>> car;
};
std::optional<Finished> take_finished(V& s, const gamedata::Summit* data);

bool enter(obj::Py& py, Player& p, int64_t cur, int64_t now_us, const gamedata::Summit* data);
std::optional<int64_t> event_slot(uint64_t mission, int64_t cur, const gamedata::Summit* data);
void post_score(obj::Py& py, Player& p, int64_t slot, int64_t score);
constexpr int64_t ESK_LEVEL_CAP = 40;
void give_tokens(obj::Py& py, V& s, int64_t count);
int64_t esk_points(int64_t rank);
void add_esk(obj::Py& py, Player& p, int64_t points, const gamedata::Summit* data);
void result(obj::Py& py, Player& p, int64_t rank, int64_t cur, int64_t now_us, const gamedata::Summit* data);

int64_t last_won(const V& s);
bool esk(obj::Py& py, Player& p, Bytes* out, Raised* raised);
int64_t boost(const Player& p, int64_t now_us);
std::optional<double> boost_factor(const Player& p, int64_t now_us, const gamedata::Summit* data);
std::pair<int64_t, int64_t> boost_wire(const Player& p, int64_t now_us, int64_t client_now_us);
int64_t medal(int64_t level, const gamedata::Summit* data);
std::optional<int64_t> spin(obj::Py& py, const Core& core, Player& p, int64_t cur, int64_t year, int64_t now_us,
                            Random& rng, const gamedata::Summit* data,
                            const std::optional<std::pair<uint64_t, uint64_t>>& car = std::nullopt);

}
}
}
