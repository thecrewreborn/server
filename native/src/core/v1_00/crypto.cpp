#include "core/v1_00/crypto.h"

#include <optional>
#include <string>

#include "core/blowfish.h"

namespace tcr {
namespace v1_00 {
namespace crypto {

namespace {

Raised key_size(const Bytes& key) {
  return Raised{"ValueError", "Invalid key size (" + std::to_string(key.size() * 8) + ") for Blowfish."};
}

}

bool encrypt(const Bytes& key, const Bytes& data, Bytes* out, Raised* raised) {
  std::optional<Bytes> got = blowfish::encrypt(key, data);
  if (!got) {
    *raised = key_size(key);
    return false;
  }
  *out = std::move(*got);
  return true;
}

bool decrypt(const Bytes& key, const Bytes& data, Bytes* out, Raised* raised) {
  std::optional<Bytes> got = blowfish::decrypt(key, data);
  if (!got) {
    *raised = key_size(key);
    return false;
  }
  *out = std::move(*got);
  return true;
}

}
}
}
