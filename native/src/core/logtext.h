#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tcr {

constexpr size_t kLogHex = 2048;
constexpr size_t kLogPath = 2048;
constexpr size_t kLogBody = 4096;

bool is_space_latin1(uint8_t c);
bool is_printable_latin1(uint8_t c);
std::vector<std::string> split_ws_latin1(std::string_view s);
std::vector<std::string> split_ws1_latin1(std::string_view s);
std::string_view strip_ws_latin1(std::string_view s);

std::string repr_latin1(std::string_view s);
std::string repr_utf8(std::string_view s);
std::string latin1_to_utf8(std::string_view s);
std::string printable(std::string_view s, size_t limit = kLogPath);
std::string printable_text(std::string_view s, size_t limit = kLogPath);

std::string json_quote_latin1(std::string_view s);
std::string json_quote_utf8(std::string_view s);

struct AuthTicket {
  std::string scheme, key, ticket;
};
std::optional<AuthTicket> auth_ticket(std::string_view auth);
std::string redact(std::string_view auth);
std::string log_path(std::string_view path);
std::string log_body(std::string_view path, std::string_view raw);

}
