#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tcr::capture {

constexpr long long kRotateBytes = 256LL * 1024 * 1024;

long long rotate_bytes();
void set_rotate_bytes(long long n);

bool append_log(const std::string& path, std::string_view line, std::string* why);
void log_failed(const std::string& path, const std::string& why);
void write_line(const std::string& path, std::string_view line);

std::string iso_now();
std::string clock_now();
void console(const std::string& text);

std::string request_line(const std::string& client_ip, std::string_view method, std::string_view path,
                         std::string_view auth, std::string_view raw, int status, std::string_view reply_json);

}
