#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/util.h"

namespace tcr {
namespace blowfish {

class Cipher {
 public:
  explicit Cipher(const Bytes& key);
  bool ok() const { return ok_; }
  void encrypt_block(uint32_t* l, uint32_t* r) const;
  void decrypt_block(uint32_t* l, uint32_t* r) const;

 private:
  uint32_t f(uint32_t x) const;
  std::array<uint32_t, 18> p_{};
  std::array<std::array<uint32_t, 256>, 4> s_{};
  bool ok_ = false;
};

std::optional<Bytes> encrypt(const Bytes& key, const uint8_t* data, size_t n);
std::optional<Bytes> decrypt(const Bytes& key, const uint8_t* data, size_t n);
inline std::optional<Bytes> encrypt(const Bytes& key, const Bytes& data) {
  return encrypt(key, data.data(), data.size());
}
inline std::optional<Bytes> decrypt(const Bytes& key, const Bytes& data) {
  return decrypt(key, data.data(), data.size());
}

}
}
