#include "core/crypto_aes.h"

#include "mbedtls/aes.h"

namespace tcr {
namespace aes {

namespace {

std::optional<Bytes> run(bool enc, const Bytes& key, const Bytes& iv, const uint8_t* data, size_t n) {
  if ((key.size() != 16 && key.size() != 24 && key.size() != 32) || iv.size() != 16) return std::nullopt;
  Bytes buf(data, data + n);
  buf.resize((n + 15) / 16 * 16, 0);
  Bytes out(buf.size());
  if (buf.empty()) return out;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  unsigned bits = static_cast<unsigned>(key.size() * 8);
  int rc = enc ? mbedtls_aes_setkey_enc(&ctx, key.data(), bits) : mbedtls_aes_setkey_dec(&ctx, key.data(), bits);
  unsigned char ivb[16];
  for (int i = 0; i < 16; i++) ivb[i] = iv[static_cast<size_t>(i)];
  if (rc == 0) {
    rc = mbedtls_aes_crypt_cbc(&ctx, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, buf.size(), ivb, buf.data(),
                               out.data());
  }
  mbedtls_aes_free(&ctx);
  if (rc != 0) return std::nullopt;
  return out;
}

}

std::optional<Bytes> encrypt(const Bytes& key, const Bytes& iv, const uint8_t* data, size_t n) {
  return run(true, key, iv, data, n);
}

std::optional<Bytes> decrypt(const Bytes& key, const Bytes& iv, const uint8_t* data, size_t n) {
  return run(false, key, iv, data, n);
}

}
}
