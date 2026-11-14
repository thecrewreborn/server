#pragma once

#include <cstdint>
#include <vector>

#include "core/accounts.h"
#include "core/util.h"
#include "core/v1_21/cars.h"

namespace tcr {
namespace v1_00 {
namespace cars {

constexpr size_t SLOTS = 25;
constexpr size_t KEPT_FIRST = 11, KEPT_END = 25;
constexpr int64_t EMPTY = 0xFFFF;
constexpr size_t OWNED_WORDS = 140;
constexpr size_t PLAYER_PARTS = 120;

int64_t parts();
int64_t perf_parts();

std::vector<int64_t> slots_from_121(const std::vector<int64_t>& s121, uint64_t variant,
                                    const v1_21::cars::Catalog* catalog);
bool config_from_121(const Bytes& cfg, const v1_21::cars::Catalog* catalog, Bytes* out, Raised* raised);
Bytes part_list(const std::vector<int64_t>& s);
bool garage_field(const v1_21::cars::Garage& entries, const v1_21::cars::Catalog* catalog, Bytes* out,
                  Raised* raised);
bool parts_field(const v1_21::cars::Garage& entries, const json::Value& owned,
                 const v1_21::cars::Catalog* catalog, Bytes* out, Raised* raised);
Bytes no_car();
bool car_reply_from_121(const Bytes& car, const v1_21::cars::Catalog* catalog, Bytes* out, Raised* raised);

}
}
}
