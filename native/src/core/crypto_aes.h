#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/util.h"

namespace tcr {
namespace aes {

std::optional<Bytes> encrypt(const Bytes& key, const Bytes& iv, const uint8_t* data, size_t n);
std::optional<Bytes> decrypt(const Bytes& key, const Bytes& iv, const uint8_t* data, size_t n);
inline std::optional<Bytes> encrypt(const Bytes& key, const Bytes& iv, const Bytes& data) {
  return encrypt(key, iv, data.data(), data.size());
}
inline std::optional<Bytes> decrypt(const Bytes& key, const Bytes& iv, const Bytes& data) {
  return decrypt(key, iv, data.data(), data.size());
}

}
}
