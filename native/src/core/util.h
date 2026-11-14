#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tcr {

using Bytes = std::vector<uint8_t>;

namespace py {

bool isspace(uint32_t cp);
bool isprintable(uint32_t cp);
int decimal(uint32_t cp);

void append_utf8(std::string& out, uint32_t cp);
std::vector<uint32_t> code_points(std::string_view text);
std::string from_code_points(const std::vector<uint32_t>& cps);
size_t length(std::string_view text);

std::string strip(std::string_view s);
std::string rstrip(std::string_view s);
std::string lower(std::string_view s);
std::string repr(std::string_view s);
std::string repr_cut(std::string_view s, size_t max_chars = 200);

struct DecodeError {
  size_t start = 0;
  size_t end = 0;
  std::string reason;
};

bool decode_utf8(const uint8_t* data, size_t n, bool surrogatepass, std::string* out, DecodeError* err);
std::string decode_utf8_replace(std::string_view data);
std::vector<uint32_t> fsdecode_code_points(std::string_view name);
bool fs_name_less(std::string_view a, std::string_view b);
std::string unquote(std::string_view s);
int hex_value(uint32_t c);
std::string decode_error_text(const char* codec, const uint8_t* data, size_t n, const DecodeError& err);
std::optional<std::string> text_file_decode_error(const uint8_t* data, size_t n, std::string* decoded = nullptr,
                                                  int code_page = 0, bool whole = false);
bool has_text_codec(int code_page);
std::string universal_newlines(std::string_view text);

std::optional<std::string> parse_int(std::string_view s, std::string* error = nullptr);
std::optional<double> parse_float(std::string_view s, std::string* error = nullptr);
std::optional<double> int_text_to_double(std::string_view decimal);
std::optional<int64_t> int_text_to_i64(std::string_view decimal);
std::optional<uint64_t> int_text_to_u64(std::string_view decimal);
std::string int_to_text(int64_t v);
std::string uint_to_text(uint64_t v);

std::optional<std::string> float_to_int_text(double f, std::string* error_type = nullptr,
                                             std::string* error = nullptr);
std::optional<int> compare_int_float(std::string_view int_text, double f);
inline int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
int compare_int_texts(std::string_view a, std::string_view b);

std::string float_repr(double v);

std::string hex(const uint8_t* data, size_t n);
inline std::string hex(const Bytes& b) { return hex(b.data(), b.size()); }
inline std::string hex(std::string_view b) { return hex(reinterpret_cast<const uint8_t*>(b.data()), b.size()); }
std::optional<Bytes> fromhex(std::string_view s, std::string* error = nullptr);

class PathRules {
 public:
  virtual ~PathRules() = default;
  virtual bool isabs(std::string_view p) const = 0;
  virtual std::string join(std::string_view a, std::string_view b) const = 0;
  virtual std::pair<std::string, std::string> split(std::string_view p) const = 0;
  std::string basename(std::string_view p) const { return split(p).second; }
};

class PosixPaths final : public PathRules {
 public:
  bool isabs(std::string_view p) const override;
  std::string join(std::string_view a, std::string_view b) const override;
  std::pair<std::string, std::string> split(std::string_view p) const override;
};

class NtPaths final : public PathRules {
 public:
  bool isabs(std::string_view p) const override;
  std::string join(std::string_view a, std::string_view b) const override;
  std::pair<std::string, std::string> split(std::string_view p) const override;
  void splitroot(std::string_view p, std::string* drive, std::string* root, std::string* tail) const;
};

const PathRules& native_paths();

}
}
