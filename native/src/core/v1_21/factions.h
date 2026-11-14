#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/accounts.h"
#include "core/json.h"
#include "core/pyobj.h"

namespace tcr {
class Core;
namespace v1_21 {
namespace factions {

constexpr int64_t HOUR = 3600;
constexpr int64_t MAX_CATCH_UP = 24 * 14;
constexpr size_t FIELD_COUNT = 20;
constexpr size_t GROUPS = 5;
extern const int64_t START[GROUPS][FIELD_COUNT];

constexpr size_t index_of(int off) { return static_cast<size_t>((off - 0x50) / 4); }

inline int64_t start_field(size_t group, int off) { return START[group][index_of(off)]; }

using Groups = json::Value;

json::Value field(obj::Py& py, const json::Value& rec, int off);

std::optional<Groups> load(Core& core, int64_t now, Raised* raised);

std::string path(Core& core);

}
}
}
