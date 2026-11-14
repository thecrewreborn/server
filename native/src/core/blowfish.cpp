#include "core/blowfish.h"

namespace tcr {
namespace blowfish {

namespace {
#include "core/blowfish_data.inc"

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}

void put_le32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

}

Cipher::Cipher(const Bytes& key) {
  if (key.size() < 4 || key.size() > 56) return;
  for (int i = 0; i < 18; i++) p_[static_cast<size_t>(i)] = kPi[i];
  for (int b = 0; b < 4; b++)
    for (int i = 0; i < 256; i++) s_[static_cast<size_t>(b)][static_cast<size_t>(i)] = kPi[18 + 256 * b + i];
  size_t k = 0;
  for (auto& p : p_) {
    uint32_t word = 0;
    for (int j = 0; j < 4; j++) {
      word = (word << 8) | key[k];
      k = (k + 1) % key.size();
    }
    p ^= word;
  }
  uint32_t l = 0, r = 0;
  for (size_t i = 0; i < 18; i += 2) {
    encrypt_block(&l, &r);
    p_[i] = l;
    p_[i + 1] = r;
  }
  for (auto& box : s_) {
    for (size_t i = 0; i < 256; i += 2) {
      encrypt_block(&l, &r);
      box[i] = l;
      box[i + 1] = r;
    }
  }
  ok_ = true;
}

uint32_t Cipher::f(uint32_t x) const {
  return ((s_[0][x >> 24] + s_[1][(x >> 16) & 0xFF]) ^ s_[2][(x >> 8) & 0xFF]) + s_[3][x & 0xFF];
}

void Cipher::encrypt_block(uint32_t* l, uint32_t* r) const {
  uint32_t xl = *l, xr = *r;
  for (size_t i = 0; i < 16; i++) {
    xl ^= p_[i];
    xr ^= f(xl);
    uint32_t t = xl;
    xl = xr;
    xr = t;
  }
  uint32_t t = xl;
  xl = xr;
  xr = t;
  xr ^= p_[16];
  xl ^= p_[17];
  *l = xl;
  *r = xr;
}

void Cipher::decrypt_block(uint32_t* l, uint32_t* r) const {
  uint32_t xl = *l, xr = *r;
  for (size_t i = 17; i > 1; i--) {
    xl ^= p_[i];
    xr ^= f(xl);
    uint32_t t = xl;
    xl = xr;
    xr = t;
  }
  uint32_t t = xl;
  xl = xr;
  xr = t;
  xr ^= p_[1];
  xl ^= p_[0];
  *l = xl;
  *r = xr;
}

std::optional<Bytes> encrypt(const Bytes& key, const uint8_t* data, size_t n) {
  Cipher c(key);
  if (!c.ok()) return std::nullopt;
  Bytes out(data, data + n);
  out.resize((n + 7) / 8 * 8, 0);
  for (size_t i = 0; i < out.size(); i += 8) {
    uint32_t l = le32(&out[i]), r = le32(&out[i + 4]);
    c.encrypt_block(&l, &r);
    put_le32(&out[i], l);
    put_le32(&out[i + 4], r);
  }
  return out;
}

std::optional<Bytes> decrypt(const Bytes& key, const uint8_t* data, size_t n) {
  Cipher c(key);
  if (!c.ok()) return std::nullopt;
  Bytes out(data, data + (n - n % 8));
  for (size_t i = 0; i < out.size(); i += 8) {
    uint32_t l = le32(&out[i]), r = le32(&out[i + 4]);
    c.decrypt_block(&l, &r);
    put_le32(&out[i], l);
    put_le32(&out[i + 4], r);
  }
  return out;
}

}
}
