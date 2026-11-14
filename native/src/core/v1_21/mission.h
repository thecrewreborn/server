#pragma once

#include <cstdint>

#include <optional>

#include "core/accounts.h"
#include "core/util.h"
#include "core/v1_21/player.h"

namespace tcr {
class Core;
namespace v1_21 {

bool finish_mission(Core& core, Player& p, uint64_t mission, int64_t score, int64_t now_us, bool paid,
                    Raised* raised);

Bytes rating(uint64_t player_id);

struct Ctx;

bool dealer_mission(const Ctx& ctx, uint64_t mission);
std::optional<bool> pays_dealer(const Ctx& ctx, const Player& p, uint64_t mission, Raised* raised);
bool dealer_car_part(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* raised);
bool owned_dealer_car_part(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* raised);
bool mission_reward_part(Ctx& ctx, Player& p, uint64_t mission, int64_t score, const Bytes& body,
                         std::optional<int64_t>* part, Raised* raised);

}
}
