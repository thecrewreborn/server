#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/v1_21/handlers.h"

namespace tcr {
class Core;
namespace v1_21 {

std::string probe_json(Core& core);

using ProbeHandle = HandleResult (*)(Ctx& ctx, uint32_t id, const Bytes& body);
std::string probe_json(Core& core, const std::vector<uint32_t>& ids, ProbeHandle fn);

}

namespace v1_00 {
std::string probe_json(Core& core);
}
}
