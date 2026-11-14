#pragma once

#include "core/accounts.h"
#include "core/util.h"

namespace tcr {
namespace v1_00 {
namespace crypto {

bool encrypt(const Bytes& key, const Bytes& data, Bytes* out, Raised* raised);
bool decrypt(const Bytes& key, const Bytes& data, Bytes* out, Raised* raised);

}
}
}
