#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tcr {

bool scrypt(const uint8_t* password, size_t password_n, const uint8_t* salt, size_t salt_n, uint64_t n, uint32_t r,
            uint32_t p, size_t dklen, std::vector<uint8_t>* out);

std::optional<std::string> account_hash(const std::string& secret, const std::vector<uint8_t>& salt);

}
