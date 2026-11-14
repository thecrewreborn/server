#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/util.h"
#include "core/v1_21/handlers.h"

namespace tcr {
class Core;
namespace v1_00 {

constexpr uint32_t GET_OR_CREATE_PLAYER = 0x5EAA9404;

std::string name_of(uint32_t id);
v1_21::HandleResult handle(v1_21::Ctx& ctx, uint32_t id, const Bytes& body);
std::vector<uint32_t> probe_ids();

}
}
