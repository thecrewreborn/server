#include "core/scrypt.h"

#include <cstring>
#include <new>

#include "core/util.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

namespace tcr {

namespace {

inline uint32_t rotl(uint32_t a, int b) { return (a << b) | (a >> (32 - b)); }

void salsa20_8(uint32_t b[16]) {
  uint32_t x[16];
  std::memcpy(x, b, sizeof(x));
  for (int i = 0; i < 8; i += 2) {
    x[4] ^= rotl(x[0] + x[12], 7);
    x[8] ^= rotl(x[4] + x[0], 9);
    x[12] ^= rotl(x[8] + x[4], 13);
    x[0] ^= rotl(x[12] + x[8], 18);
    x[9] ^= rotl(x[5] + x[1], 7);
    x[13] ^= rotl(x[9] + x[5], 9);
    x[1] ^= rotl(x[13] + x[9], 13);
    x[5] ^= rotl(x[1] + x[13], 18);
    x[14] ^= rotl(x[10] + x[6], 7);
    x[2] ^= rotl(x[14] + x[10], 9);
    x[6] ^= rotl(x[2] + x[14], 13);
    x[10] ^= rotl(x[6] + x[2], 18);
    x[3] ^= rotl(x[15] + x[11], 7);
    x[7] ^= rotl(x[3] + x[15], 9);
    x[11] ^= rotl(x[7] + x[3], 13);
    x[15] ^= rotl(x[11] + x[7], 18);
    x[1] ^= rotl(x[0] + x[3], 7);
    x[2] ^= rotl(x[1] + x[0], 9);
    x[3] ^= rotl(x[2] + x[1], 13);
    x[0] ^= rotl(x[3] + x[2], 18);
    x[6] ^= rotl(x[5] + x[4], 7);
    x[7] ^= rotl(x[6] + x[5], 9);
    x[4] ^= rotl(x[7] + x[6], 13);
    x[5] ^= rotl(x[4] + x[7], 18);
    x[11] ^= rotl(x[10] + x[9], 7);
    x[8] ^= rotl(x[11] + x[10], 9);
    x[9] ^= rotl(x[8] + x[11], 13);
    x[10] ^= rotl(x[9] + x[8], 18);
    x[12] ^= rotl(x[15] + x[14], 7);
    x[13] ^= rotl(x[12] + x[15], 9);
    x[14] ^= rotl(x[13] + x[12], 13);
    x[15] ^= rotl(x[14] + x[13], 18);
  }
  for (int i = 0; i < 16; i++) b[i] += x[i];
}

void block_mix(const uint32_t* in, uint32_t* out, uint32_t r) {
  uint32_t x[16];
  std::memcpy(x, in + (2 * r - 1) * 16, sizeof(x));
  for (uint32_t i = 0; i < 2 * r; i++) {
    for (int k = 0; k < 16; k++) x[k] ^= in[i * 16 + k];
    salsa20_8(x);
    uint32_t* dst = out + ((i & 1) ? (r + i / 2) : (i / 2)) * 16;
    std::memcpy(dst, x, sizeof(x));
  }
}

void ro_mix(uint8_t* block, uint32_t r, uint64_t n, uint32_t* v, uint32_t* x, uint32_t* y) {
  const size_t words = 32 * static_cast<size_t>(r);
  for (size_t k = 0; k < words; k++) {
    const uint8_t* p = block + 4 * k;
    x[k] = static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
  }
  for (uint64_t i = 0; i < n; i++) {
    std::memcpy(v + i * words, x, words * 4);
    block_mix(x, y, r);
    std::memcpy(x, y, words * 4);
  }
  for (uint64_t i = 0; i < n; i++) {
    uint64_t j = x[(2 * r - 1) * 16] & (n - 1);
    const uint32_t* vj = v + j * words;
    for (size_t k = 0; k < words; k++) x[k] ^= vj[k];
    block_mix(x, y, r);
    std::memcpy(x, y, words * 4);
  }
  for (size_t k = 0; k < words; k++) {
    uint8_t* p = block + 4 * k;
    p[0] = static_cast<uint8_t>(x[k]);
    p[1] = static_cast<uint8_t>(x[k] >> 8);
    p[2] = static_cast<uint8_t>(x[k] >> 16);
    p[3] = static_cast<uint8_t>(x[k] >> 24);
  }
}

}

bool scrypt(const uint8_t* password, size_t password_n, const uint8_t* salt, size_t salt_n, uint64_t n, uint32_t r,
            uint32_t p, size_t dklen, std::vector<uint8_t>* out) {
  if (n < 2 || (n & (n - 1)) != 0 || n > (1ull << 32) || r == 0 || p == 0 || dklen == 0) return false;
  const size_t block = 128 * static_cast<size_t>(r);
  std::vector<uint8_t> b(block * p);
  static const uint8_t kEmpty = 0;
  if (!password_n) password = &kEmpty;
  if (!salt_n) salt = &kEmpty;
  if (mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, password, password_n, salt, salt_n, 1,
                                    static_cast<uint32_t>(b.size()), b.data()) != 0) {
    return false;
  }
  const size_t words = block / 4;
  uint32_t* v = new (std::nothrow) uint32_t[words * n];
  if (!v) return false;
  std::vector<uint32_t> x(words), y(words);
  for (uint32_t i = 0; i < p; i++) ro_mix(b.data() + i * block, r, n, v, x.data(), y.data());
  delete[] v;
  out->assign(dklen, 0);
  return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, password, password_n, b.data(), b.size(), 1,
                                       static_cast<uint32_t>(dklen), out->data()) == 0;
}

std::optional<std::string> account_hash(const std::string& secret, const std::vector<uint8_t>& salt) {
  std::vector<uint8_t> out;
  if (!scrypt(reinterpret_cast<const uint8_t*>(secret.data()), secret.size(), salt.data(), salt.size(), 1u << 14, 8,
              1, 32, &out)) {
    return std::nullopt;
  }
  return py::hex(out);
}

}
