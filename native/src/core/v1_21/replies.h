#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "core/util.h"
#include "core/v1_21/handlers.h"

namespace tcr {
namespace v1_21 {
namespace replies {

std::optional<Bytes> fill(const Ctx& ctx, const Bytes& body,
                          const std::vector<std::pair<size_t, std::string>>& fields, Raised* raised);

const std::map<std::pair<std::string, int>, std::string>& service_by_addr();

}
}
}
