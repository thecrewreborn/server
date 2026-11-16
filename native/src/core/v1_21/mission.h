#pragma once

#include <cstdint>

#include <optional>

#include "core/accounts.h"
#include "core/util.h"
#include "core/v1_21/player.h"

namespace tcr {
class Core;
namespace v1_21 {

constexpr int64_t NO_SCORE = -1;
constexpr size_t FACTION_CAR_AT = 0x14, PRACTICE_CAR_AT = 0x2B;
bool pay_faction_mission(Core& core, Player& p, const Bytes& body, int64_t now_us, int64_t group, Raised* raised);
std::optional<bool> pay_gifts(Core& core, Player& p, Raised* raised);
bool finish_mission(Core& core, Player& p, uint64_t mission, int64_t score, int64_t now_us, bool paid,
                    Raised* raised, int64_t group = 1, const Bytes* body = nullptr, size_t car_at = 0x28,
                    std::optional<int64_t>* part = nullptr);

struct InstantEnd;
constexpr size_t IC_SEEN = 64;
bool pay_instant_challenge(Core& core, Player& p, const InstantEnd& e, int64_t now_us,
                           std::optional<std::pair<int64_t, int64_t>>* paid, Raised* raised);

Bytes rating(uint64_t player_id);

struct Ctx;

bool dealer_mission(const Ctx& ctx, uint64_t mission);
std::optional<bool> pays_dealer(const Ctx& ctx, const Player& p, uint64_t mission, Raised* raised);
bool dealer_car_part(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* raised);
bool owned_dealer_car_part(Ctx& ctx, Player& p, const Bytes& body, std::optional<Bytes>* car, Raised* raised);

}
}
