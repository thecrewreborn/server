#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/util.h"

namespace tcr {
namespace codec {

std::optional<Bytes> encode_default(const std::vector<std::string>& layout,
                                    const std::map<size_t, Bytes>& overrides = {});

struct Item {
  bool is_bytes = false;
  uint64_t value = 0;
  Bytes bytes;
};

std::optional<std::vector<Item>> decode(const std::vector<std::string>& layout, const Bytes& body);

}
}
