#include "core/util.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(TCR_FLOAT_CHARCONV_FALLBACK) && defined(__APPLE__) && \
    defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 260000
#define TCR_FLOAT_CHARCONV_FALLBACK 1
#endif

namespace tcr {
namespace py {

namespace {
#include "core/unicode_data.inc"

#if TCR_FLOAT_CHARCONV_FALLBACK
double c_strtod(std::string_view text) { return std::strtod(std::string(text).c_str(), nullptr); }

void step_digits(std::string& digits, int& exp10, bool up) {
  size_t i = digits.size();
  if (up) {
    while (i > 0 && digits[i - 1] == '9') digits[--i] = '0';
    if (i == 0) {
      digits[0] = '1';
      exp10++;
    } else {
      digits[i - 1]++;
    }
  } else {
    while (i > 0 && digits[i - 1] == '0') digits[--i] = '9';
    digits[i - 1]--;
    if (digits[0] == '0') {
      digits.assign(digits.size(), '9');
      exp10--;
    }
  }
}

std::string scientific_text(bool neg, const std::string& digits, int exp10) {
  std::string out = neg ? "-" : "";
  out += digits[0];
  if (digits.size() > 1) {
    out += '.';
    out += digits.substr(1);
  }
  out += exp10 < 0 ? "e-" : "e+";
  int a = exp10 < 0 ? -exp10 : exp10;
  if (a < 10) out += '0';
  out += std::to_string(a);
  return out;
}

std::string shortest_scientific(double v) {
  char buf[64];
  for (int p = 0; p < 17; p++) {
    std::snprintf(buf, sizeof(buf), "%.*e", p, v);
    double back = std::strtod(buf, nullptr);
    if (back == v) return buf;
    std::string_view t(buf);
    bool neg = t[0] == '-';
    if (neg) t.remove_prefix(1);
    size_t e = t.find('e');
    std::string digits;
    for (char c : t.substr(0, e))
      if (c != '.') digits += c;
    int exp10 = std::atoi(std::string(t.substr(e + 1)).c_str());
    bool up = std::fabs(back) < std::fabs(v);
    step_digits(digits, exp10, up);
    std::string other = scientific_text(neg, digits, exp10);
    if (std::strtod(other.c_str(), nullptr) == v) return other;
  }
  std::snprintf(buf, sizeof(buf), "%.16e", v);
  return buf;
}
#endif

constexpr size_t kMaxIntDigits = 4300;

bool ascii_space(uint32_t c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool ascii_digit(uint32_t c) { return c >= '0' && c <= '9'; }

const char kHex[] = "0123456789abcdef";

void append_hex(std::string& out, uint32_t v, int digits) {
  for (int i = digits - 1; i >= 0; i--) out += kHex[(v >> (4 * i)) & 0xF];
}

std::string to_ascii_number_text(std::string_view s) {
  std::string out;
  for (uint32_t cp : code_points(s)) {
    if (cp < 127) {
      out += static_cast<char>(cp);
    } else if (isspace(cp)) {
      out += ' ';
    } else {
      int d = decimal(cp);
      if (d < 0) {
        out += '?';
        break;
      }
      out += static_cast<char>('0' + d);
    }
  }
  return out;
}

}

bool isspace(uint32_t cp) {
  const size_t n = sizeof(kSpace) / sizeof(kSpace[0]);
  return std::binary_search(kSpace, kSpace + n, cp);
}

bool isprintable(uint32_t cp) {
  size_t lo = 0, hi = sizeof(kNotPrintable) / sizeof(kNotPrintable[0]);
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (cp < kNotPrintable[mid][0]) {
      hi = mid;
    } else if (cp > kNotPrintable[mid][1]) {
      lo = mid + 1;
    } else {
      return false;
    }
  }
  return true;
}

int decimal(uint32_t cp) {
  if (cp >= '0' && cp <= '9') return static_cast<int>(cp - '0');
  for (const auto& run : kDecimal) {
    if (cp >= run[0] && cp < run[0] + run[2]) return static_cast<int>(run[1] + (cp - run[0]));
  }
  return -1;
}

void append_utf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::vector<uint32_t> code_points(std::string_view text) {
  std::vector<uint32_t> out;
  out.reserve(text.size());
  const auto* p = reinterpret_cast<const uint8_t*>(text.data());
  size_t n = text.size(), i = 0;
  while (i < n) {
    uint8_t b = p[i];
    int len = b < 0x80 ? 1 : b < 0xC0 ? 0 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF8 ? 4 : 0;
    if (len == 0 || i + len > n) {
      out.push_back(0xFFFD);
      i++;
      continue;
    }
    uint32_t cp = len == 1 ? b : len == 2 ? (b & 0x1F) : len == 3 ? (b & 0x0F) : (b & 0x07);
    for (int k = 1; k < len; k++) cp = (cp << 6) | (p[i + k] & 0x3F);
    out.push_back(cp);
    i += len;
  }
  return out;
}

std::string from_code_points(const std::vector<uint32_t>& cps) {
  std::string out;
  for (uint32_t cp : cps) append_utf8(out, cp);
  return out;
}

size_t length(std::string_view text) {
  size_t n = 0;
  for (char c : text)
    if ((static_cast<uint8_t>(c) & 0xC0) != 0x80) n++;
  return n;
}

std::string strip(std::string_view s) {
  std::vector<uint32_t> cps = code_points(s);
  size_t a = 0, b = cps.size();
  while (a < b && isspace(cps[a])) a++;
  while (b > a && isspace(cps[b - 1])) b--;
  return from_code_points(std::vector<uint32_t>(cps.begin() + a, cps.begin() + b));
}

std::string rstrip(std::string_view s) {
  std::vector<uint32_t> cps = code_points(s);
  size_t b = cps.size();
  while (b > 0 && isspace(cps[b - 1])) b--;
  cps.resize(b);
  return from_code_points(cps);
}

namespace {

template <size_t N>
bool in_ranges(const uint32_t (&table)[N][2], uint32_t cp) {
  size_t lo = 0, hi = N;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (cp < table[mid][0]) {
      hi = mid;
    } else if (cp > table[mid][1]) {
      lo = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

bool final_sigma(const std::vector<uint32_t>& cps, size_t i) {
  size_t j = i;
  uint32_t c = 0;
  bool found = false;
  while (j > 0) {
    c = cps[--j];
    if (!in_ranges(kCaseIgnorable, c)) {
      found = true;
      break;
    }
  }
  bool fin = found && in_ranges(kCased, c);
  if (fin && i + 1 < cps.size()) {
    size_t k = i + 1;
    for (; k < cps.size(); k++) {
      c = cps[k];
      if (!in_ranges(kCaseIgnorable, c)) break;
    }
    fin = k == cps.size() || !in_ranges(kCased, c);
  }
  return fin;
}

}

std::string lower(std::string_view s) {
  std::string out;
  const size_t n = sizeof(kLower) / sizeof(kLower[0]);
  std::vector<uint32_t> cps = code_points(s);
  for (size_t i = 0; i < cps.size(); i++) {
    uint32_t cp = cps[i];
    if (cp >= 'A' && cp <= 'Z') {
      out += static_cast<char>(cp + 32);
      continue;
    }
    if (cp < 0x80) {
      out += static_cast<char>(cp);
      continue;
    }
    if (cp == 0x3A3) {
      append_utf8(out, final_sigma(cps, i) ? 0x3C2 : 0x3C3);
      continue;
    }
    size_t lo = 0, hi = n;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (kLower[mid][0] < cp) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo < n && kLower[lo][0] == cp) {
      for (int k = 1; k < 4 && kLower[lo][k]; k++) append_utf8(out, kLower[lo][k]);
    } else {
      append_utf8(out, cp);
    }
  }
  return out;
}

std::string repr(std::string_view s) {
  std::vector<uint32_t> cps = code_points(s);
  bool single = false, dbl = false;
  for (uint32_t c : cps) {
    if (c == '\'') single = true;
    if (c == '"') dbl = true;
  }
  const char quote = single && !dbl ? '"' : '\'';
  std::string out(1, quote);
  for (uint32_t c : cps) {
    if (c == static_cast<uint32_t>(quote) || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c == '\t') {
      out += "\\t";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c < ' ' || c == 0x7F) {
      out += "\\x";
      append_hex(out, c, 2);
    } else if (c < 0x7F) {
      out += static_cast<char>(c);
    } else if (isprintable(c)) {
      append_utf8(out, c);
    } else if (c <= 0xFF) {
      out += "\\x";
      append_hex(out, c, 2);
    } else if (c <= 0xFFFF) {
      out += "\\u";
      append_hex(out, c, 4);
    } else {
      out += "\\U";
      append_hex(out, c, 8);
    }
  }
  out += quote;
  return out;
}

std::string repr_cut(std::string_view s, size_t max_chars) {
  std::vector<uint32_t> cps = code_points(repr(s));
  if (cps.size() > max_chars) cps.resize(max_chars);
  return from_code_points(cps);
}

namespace {

bool cont(uint8_t b) { return (b & 0xC0) == 0x80; }

bool utf8_decode_impl(const uint8_t* data, size_t n, bool surrogatepass, bool final, std::string* out,
                      DecodeError* err, size_t* consumed) {
  size_t i = 0;
  while (i < n) {
    uint8_t ch = data[i];
    if (ch < 0x80) {
      if (out) out->push_back(static_cast<char>(ch));
      i++;
      continue;
    }
    size_t left = n - i;
    int kind = 1;
    uint32_t cp = 0;
    size_t len = 0;
    if (ch < 0xC2) {
      kind = 1;
    } else if (ch < 0xE0) {
      if (left < 2) {
        kind = 0;
      } else if (!cont(data[i + 1])) {
        kind = 2;
      } else {
        cp = ((ch & 0x1Fu) << 6) | (data[i + 1] & 0x3Fu);
        len = 2;
      }
    } else if (ch < 0xF0) {
      if (left < 3) {
        if (left < 2) {
          kind = 0;
        } else {
          uint8_t c2 = data[i + 1];
          kind = (!cont(c2) || (c2 < 0xA0 ? ch == 0xE0 : ch == 0xED)) ? 2 : 0;
        }
      } else {
        uint8_t c2 = data[i + 1], c3 = data[i + 2];
        if (!cont(c2) || (ch == 0xE0 && c2 < 0xA0) || (ch == 0xED && c2 >= 0xA0)) {
          kind = 2;
        } else if (!cont(c3)) {
          kind = 3;
        } else {
          cp = ((ch & 0x0Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
          len = 3;
        }
      }
    } else if (ch < 0xF5) {
      if (left < 4) {
        if (left < 2) {
          kind = 0;
        } else {
          uint8_t c2 = data[i + 1];
          if (!cont(c2) || (c2 < 0x90 ? ch == 0xF0 : ch == 0xF4)) {
            kind = 2;
          } else if (left < 3) {
            kind = 0;
          } else {
            kind = cont(data[i + 2]) ? 0 : 3;
          }
        }
      } else {
        uint8_t c2 = data[i + 1], c3 = data[i + 2], c4 = data[i + 3];
        if (!cont(c2) || (ch == 0xF0 && c2 < 0x90) || (ch == 0xF4 && c2 >= 0x90)) {
          kind = 2;
        } else if (!cont(c3)) {
          kind = 3;
        } else if (!cont(c4)) {
          kind = 4;
        } else {
          cp = ((ch & 0x07u) << 18) | ((c2 & 0x3Fu) << 12) | ((c3 & 0x3Fu) << 6) | (c4 & 0x3Fu);
          len = 4;
        }
      }
    } else {
      kind = 1;
    }
    if (len) {
      if (out) append_utf8(*out, cp);
      i += len;
      continue;
    }
    if (kind == 0 && !final) break;
    if (kind == 2 && !final && ch == 0xED && left == 2 && data[i + 1] >= 0xA0 && data[i + 1] <= 0xBF) break;
    if (surrogatepass && left >= 3 && (ch & 0xF0) == 0xE0 && cont(data[i + 1]) && cont(data[i + 2])) {
      uint32_t s = ((ch & 0x0Fu) << 12) | ((data[i + 1] & 0x3Fu) << 6) | (data[i + 2] & 0x3Fu);
      if (s >= 0xD800 && s <= 0xDFFF) {
        if (out) append_utf8(*out, s);
        i += 3;
        continue;
      }
    }
    if (err) {
      err->start = i;
      if (kind == 0) {
        err->end = n;
        err->reason = "unexpected end of data";
      } else if (kind == 1) {
        err->end = i + 1;
        err->reason = "invalid start byte";
      } else {
        err->end = i + static_cast<size_t>(kind) - 1;
        err->reason = "invalid continuation byte";
      }
    }
    if (consumed) *consumed = i;
    return false;
  }
  if (consumed) *consumed = i;
  return true;
}

}

bool decode_utf8(const uint8_t* data, size_t n, bool surrogatepass, std::string* out, DecodeError* err) {
  if (out) out->clear();
  return utf8_decode_impl(data, n, surrogatepass, true, out, err, nullptr);
}

int hex_value(uint32_t c) {
  if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A' + 10);
  return -1;
}

std::string decode_utf8_replace(std::string_view data) {
  std::string out;
  size_t pos = 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
  while (pos < data.size()) {
    std::string part;
    DecodeError err;
    if (decode_utf8(p + pos, data.size() - pos, false, &part, &err)) {
      out += part;
      break;
    }
    if (err.start) {
      std::string head;
      DecodeError ignored;
      decode_utf8(p + pos, err.start, false, &head, &ignored);
      out += head;
    }
    append_utf8(out, 0xFFFD);
    pos += err.end;
  }
  return out;
}

std::vector<uint32_t> fsdecode_code_points(std::string_view name) {
  std::vector<uint32_t> out;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(name.data());
  size_t pos = 0;
  while (pos < name.size()) {
    std::string part;
    DecodeError err;
    bool ok = decode_utf8(p + pos, name.size() - pos, false, &part, &err);
    if (!ok && err.start) {
      DecodeError ignored;
      decode_utf8(p + pos, err.start, false, &part, &ignored);
    }
    for (uint32_t cp : code_points(part)) out.push_back(cp);
    if (ok) break;
    for (size_t k = err.start; k < err.end; k++) out.push_back(0xDC00 + p[pos + k]);
    pos += err.end;
  }
  return out;
}

bool fs_name_less(std::string_view a, std::string_view b) { return fsdecode_code_points(a) < fsdecode_code_points(b); }

namespace {

std::string unquote_bytes(std::string_view s) {
  size_t pct = s.find('%');
  if (pct == std::string_view::npos) return std::string(s);
  std::string out(s.substr(0, pct));
  size_t i = pct;
  while (i < s.size()) {
    size_t next = s.find('%', i + 1);
    std::string_view item = s.substr(i + 1, next == std::string_view::npos ? std::string_view::npos : next - i - 1);
    if (item.size() >= 2 && hex_value(static_cast<uint8_t>(item[0])) >= 0 &&
        hex_value(static_cast<uint8_t>(item[1])) >= 0) {
      out += static_cast<char>(hex_value(static_cast<uint8_t>(item[0])) * 16 + hex_value(static_cast<uint8_t>(item[1])));
      out.append(item.substr(2));
    } else {
      out += '%';
      out.append(item);
    }
    if (next == std::string_view::npos) break;
    i = next;
  }
  return out;
}

}

std::string unquote(std::string_view s) {
  if (s.find('%') == std::string_view::npos) return std::string(s);
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = i;
    bool ascii = static_cast<uint8_t>(s[i]) < 0x80;
    while (j < s.size() && (static_cast<uint8_t>(s[j]) < 0x80) == ascii) j++;
    std::string_view run = s.substr(i, j - i);
    out += ascii ? decode_utf8_replace(unquote_bytes(run)) : std::string(run);
    i = j;
  }
  return out;
}

std::string decode_error_text(const char* codec, const uint8_t* data, size_t n, const DecodeError& err) {
  char buf[256];
  if (err.end == err.start + 1 && err.start < n) {
    std::snprintf(buf, sizeof(buf), "'%s' codec can't decode byte 0x%02x in position %zu: ", codec, data[err.start],
                  err.start);
  } else {
    std::snprintf(buf, sizeof(buf), "'%s' codec can't decode bytes in position %zu-%zu: ", codec, err.start,
                  err.end - 1);
  }
  return std::string(buf) + err.reason;
}

namespace {

#include "core/codepages.inc"

const uint16_t* code_page_table(int code_page) {
  switch (code_page) {
    case 874: return kCp874;
    case 1250: return kCp1250;
    case 1251: return kCp1251;
    case 1252: return kCp1252;
    case 1253: return kCp1253;
    case 1254: return kCp1254;
    case 1255: return kCp1255;
    case 1256: return kCp1256;
    case 1257: return kCp1257;
    case 1258: return kCp1258;
    default: return nullptr;
  }
}

}

bool has_text_codec(int code_page) { return code_page == 0 || code_page == 65001 || code_page_table(code_page); }

std::string universal_newlines(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\r') {
      out += '\n';
      if (i + 1 < text.size() && text[i + 1] == '\n') i++;
    } else {
      out += text[i];
    }
  }
  return out;
}

std::optional<std::string> text_file_decode_error(const uint8_t* data, size_t n, std::string* decoded, int code_page,
                                                  bool whole) {
  const size_t kChunk = 8192;
  if (decoded) decoded->clear();
  if (const uint16_t* table = code_page_table(code_page)) {
    std::string text;
    for (size_t i = 0; i < n; i++) {
      if (!whole && i % kChunk == 0 && decoded) {
        *decoded += text;
        text.clear();
      }
      uint32_t cp = data[i] < 0x80 ? data[i] : table[data[i] - 0x80];
      if (data[i] >= 0x80 && !cp) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "'charmap' codec can't decode byte 0x%02x in position %zu: character maps to <undefined>",
                      data[i], whole ? i : i % kChunk);
        return std::string(buf);
      }
      append_utf8(text, cp);
    }
    if (decoded) *decoded += text;
    return std::nullopt;
  }
  if (whole) {
    DecodeError err;
    std::string text;
    if (!decode_utf8(data, n, false, &text, &err)) return decode_error_text("utf-8", data, n, err);
    if (decoded) *decoded = std::move(text);
    return std::nullopt;
  }
  std::vector<uint8_t> pending;
  size_t pos = 0;
  while (true) {
    size_t take = std::min(kChunk, n - pos);
    bool final = take == 0;
    std::vector<uint8_t> input(pending);
    input.insert(input.end(), data + pos, data + pos + take);
    pos += take;
    DecodeError err;
    size_t consumed = 0;
    std::string text;
    if (!utf8_decode_impl(input.data(), input.size(), false, final, &text, &err, &consumed)) {
      return decode_error_text("utf-8", input.data(), input.size(), err);
    }
    if (decoded) *decoded += text;
    if (final) return std::nullopt;
    pending.assign(input.begin() + static_cast<std::ptrdiff_t>(consumed), input.end());
  }
}

std::optional<std::string> parse_int(std::string_view s, std::string* error) {
  auto fail = [&]() -> std::optional<std::string> {
    if (error) *error = "invalid literal for int() with base 10: " + repr_cut(s);
    return std::nullopt;
  };
  std::string t = to_ascii_number_text(s);
  const size_t nul = t.find('\0');
  const bool has_nul = nul != std::string::npos;
  if (has_nul) t.resize(nul);
  size_t i = 0, n = t.size();
  while (i < n && ascii_space(static_cast<uint8_t>(t[i]))) i++;
  bool neg = false;
  if (i < n && (t[i] == '+' || t[i] == '-')) {
    neg = t[i] == '-';
    i++;
  }
  std::string digits;
  char prev = 0;
  while (i < n && (ascii_digit(static_cast<uint8_t>(t[i])) || t[i] == '_')) {
    if (t[i] == '_') {
      if (!ascii_digit(static_cast<uint8_t>(prev))) return fail();
    } else {
      digits += t[i];
    }
    prev = t[i];
    i++;
  }
  if (prev == '_' || digits.empty()) return fail();
  while (i < n && ascii_space(static_cast<uint8_t>(t[i]))) i++;
  if (i != n) return fail();
  if (digits.size() > kMaxIntDigits) {
    if (error) {
      *error = "Exceeds the limit (4300 digits) for integer string conversion: value has " +
               std::to_string(digits.size()) + " digits; use sys.set_int_max_str_digits() to increase the limit";
    }
    return std::nullopt;
  }
  if (has_nul) return fail();
  size_t z = 0;
  while (z + 1 < digits.size() && digits[z] == '0') z++;
  digits.erase(0, z);
  if (digits == "0") return std::string("0");
  return neg ? "-" + digits : digits;
}

namespace {

double decimal_to_double(std::string_view text) {
#if TCR_FLOAT_CHARCONV_FALLBACK
  return c_strtod(text);
#else
  double v = 0.0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), v, std::chars_format::general);
  if (r.ec == std::errc()) return v;
  size_t e = text.find_first_of("eE");
  std::string_view mant = text.substr(0, e);
  long long exp10 = 0;
  if (e != std::string_view::npos) {
    size_t k = e + 1;
    bool eneg = false;
    if (k < text.size() && (text[k] == '+' || text[k] == '-')) eneg = text[k++] == '-';
    for (; k < text.size(); k++) {
      if (exp10 < 100000000) exp10 = exp10 * 10 + (text[k] - '0');
    }
    if (eneg) exp10 = -exp10;
  }
  size_t dot = mant.find('.');
  std::string_view ip = mant.substr(0, dot);
  std::string_view fp = dot == std::string_view::npos ? std::string_view() : mant.substr(dot + 1);
  long long pos;
  size_t nz = ip.find_first_not_of('0');
  if (nz != std::string_view::npos) {
    pos = static_cast<long long>(ip.size() - nz);
  } else {
    size_t fz = fp.find_first_not_of('0');
    if (fz == std::string_view::npos) return 0.0;
    pos = -static_cast<long long>(fz);
  }
  return pos + exp10 > 0 ? HUGE_VAL : 0.0;
#endif
}

}

std::optional<double> parse_float(std::string_view s, std::string* error) {
  auto fail = [&]() -> std::optional<double> {
    if (error) *error = "could not convert string to float: " + repr(s);
    return std::nullopt;
  };
  std::string t = to_ascii_number_text(s);
  if (t.find('\0') != std::string::npos) return fail();
  size_t a = 0, b = t.size();
  while (a < b && ascii_space(static_cast<uint8_t>(t[a]))) a++;
  while (b > a && ascii_space(static_cast<uint8_t>(t[b - 1]))) b--;
  std::string body = t.substr(a, b - a);
  if (body.find('_') != std::string::npos) {
    std::string clean;
    char prev = 0;
    for (char c : body) {
      if (c == '_') {
        if (!ascii_digit(static_cast<uint8_t>(prev))) return fail();
      } else {
        if (prev == '_' && !ascii_digit(static_cast<uint8_t>(c))) return fail();
        clean += c;
      }
      prev = c;
    }
    if (prev == '_') return fail();
    body = clean;
  }
  if (body.empty()) return fail();
  bool neg = false;
  size_t i = 0;
  if (body[0] == '+' || body[0] == '-') {
    neg = body[0] == '-';
    i = 1;
  }
  std::string rest = body.substr(i);
  std::string low;
  for (char c : rest) low += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  if (low == "inf" || low == "infinity") return neg ? -HUGE_VAL : HUGE_VAL;
  if (low == "nan") return neg ? -std::nan("") : std::nan("");
  size_t k = 0, nd = 0;
  while (k < rest.size() && ascii_digit(static_cast<uint8_t>(rest[k]))) {
    k++;
    nd++;
  }
  if (k < rest.size() && rest[k] == '.') {
    k++;
    while (k < rest.size() && ascii_digit(static_cast<uint8_t>(rest[k]))) {
      k++;
      nd++;
    }
  }
  if (nd == 0) return fail();
  if (k < rest.size() && (rest[k] == 'e' || rest[k] == 'E')) {
    size_t e = k + 1;
    if (e < rest.size() && (rest[e] == '+' || rest[e] == '-')) e++;
    size_t ed = e;
    while (ed < rest.size() && ascii_digit(static_cast<uint8_t>(rest[ed]))) ed++;
    if (ed == e) return fail();
    k = ed;
  }
  if (k != rest.size()) return fail();
  double v = decimal_to_double(rest);
  return neg ? -v : v;
}

std::optional<double> int_text_to_double(std::string_view decimal) {
  bool neg = !decimal.empty() && decimal[0] == '-';
  std::string_view digits = neg ? decimal.substr(1) : decimal;
#if TCR_FLOAT_CHARCONV_FALLBACK
  size_t n = std::min(digits.find_first_not_of("0123456789"), digits.size());
  if (n == 0) return std::nullopt;
  double v = c_strtod(digits.substr(0, n));
  if (std::isinf(v)) return std::nullopt;
#else
  double v = 0.0;
  auto r = std::from_chars(digits.data(), digits.data() + digits.size(), v, std::chars_format::fixed);
  if (r.ec != std::errc() || std::isinf(v)) return std::nullopt;
#endif
  return neg ? -v : v;
}

std::optional<int64_t> int_text_to_i64(std::string_view decimal) {
  int64_t v = 0;
  auto r = std::from_chars(decimal.data(), decimal.data() + decimal.size(), v);
  if (r.ec != std::errc() || r.ptr != decimal.data() + decimal.size()) return std::nullopt;
  return v;
}

std::optional<uint64_t> int_text_to_u64(std::string_view decimal) {
  uint64_t v = 0;
  auto r = std::from_chars(decimal.data(), decimal.data() + decimal.size(), v);
  if (r.ec != std::errc() || r.ptr != decimal.data() + decimal.size()) return std::nullopt;
  return v;
}

std::string int_to_text(int64_t v) {
  char buf[32];
  auto r = std::to_chars(buf, buf + sizeof(buf), v);
  return std::string(buf, r.ptr);
}

std::string uint_to_text(uint64_t v) {
  char buf[32];
  auto r = std::to_chars(buf, buf + sizeof(buf), v);
  return std::string(buf, r.ptr);
}

std::string float_repr(double v) {
  if (std::isnan(v)) return "nan";
  if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
#if TCR_FLOAT_CHARCONV_FALLBACK
  std::string sci_text = shortest_scientific(v);
  std::string_view sci(sci_text);
#else
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::scientific);
  std::string_view sci(buf, static_cast<size_t>(r.ptr - buf));
#endif
  std::string out;
  if (sci[0] == '-') {
    out += '-';
    sci.remove_prefix(1);
  }
  size_t e = sci.find('e');
  std::string digits;
  for (char c : sci.substr(0, e))
    if (c != '.') digits += c;
  int exp10 = 0;
  std::from_chars(sci.data() + e + (sci[e + 1] == '+' ? 2 : 1), sci.data() + sci.size(), exp10);
  int decpt = exp10 + 1;
  if (decpt <= -4 || decpt > 16) {
    out += digits[0];
    if (digits.size() > 1) {
      out += '.';
      out += digits.substr(1);
    }
    out += 'e';
    out += exp10 < 0 ? '-' : '+';
    int a = exp10 < 0 ? -exp10 : exp10;
    if (a < 10) out += '0';
    out += std::to_string(a);
  } else if (decpt <= 0) {
    out += "0.";
    out.append(static_cast<size_t>(-decpt), '0');
    out += digits;
  } else if (static_cast<size_t>(decpt) >= digits.size()) {
    out += digits;
    out.append(static_cast<size_t>(decpt) - digits.size(), '0');
    out += ".0";
  } else {
    out += digits.substr(0, static_cast<size_t>(decpt));
    out += '.';
    out += digits.substr(static_cast<size_t>(decpt));
  }
  return out;
}

std::string hex(const uint8_t* data, size_t n) {
  std::string out;
  out.reserve(2 * n);
  for (size_t i = 0; i < n; i++) {
    out += kHex[data[i] >> 4];
    out += kHex[data[i] & 0xF];
  }
  return out;
}

std::optional<Bytes> fromhex(std::string_view s, std::string* error) {
  std::vector<uint32_t> cps = code_points(s);
  auto bad = [&](size_t at) -> std::optional<Bytes> {
    if (error) *error = "non-hexadecimal number found in fromhex() arg at position " + std::to_string(at);
    return std::nullopt;
  };
  for (size_t i = 0; i < cps.size(); i++)
    if (cps[i] >= 128) return bad(i);
  Bytes out;
  size_t i = 0, n = cps.size();
  while (i < n) {
    if (ascii_space(cps[i])) {
      while (i < n && ascii_space(cps[i])) i++;
      if (i >= n) break;
    }
    int top = hex_value(cps[i]);
    if (top < 0) return bad(i);
    i++;
    if (i >= n) return bad(n);
    int bot = hex_value(cps[i]);
    if (bot < 0) return bad(i);
    i++;
    out.push_back(static_cast<uint8_t>((top << 4) | bot));
  }
  return out;
}

bool PosixPaths::isabs(std::string_view p) const { return !p.empty() && p[0] == '/'; }

std::string PosixPaths::join(std::string_view a, std::string_view b) const {
  if (isabs(b)) return std::string(b);
  std::string out(a);
  if (!out.empty() && out.back() != '/') out += '/';
  out += b;
  return out;
}

std::pair<std::string, std::string> PosixPaths::split(std::string_view p) const {
  size_t i = p.rfind('/');
  i = i == std::string_view::npos ? 0 : i + 1;
  std::string head(p.substr(0, i)), tail(p.substr(i));
  if (!head.empty() && head.find_first_not_of('/') != std::string::npos) {
    while (!head.empty() && head.back() == '/') head.pop_back();
  }
  return {head, tail};
}

namespace {

using Cps = std::vector<uint32_t>;

Cps slice(const Cps& v, size_t a, size_t b = static_cast<size_t>(-1)) {
  if (a >= v.size()) return Cps();
  b = std::min(b, v.size());
  return b <= a ? Cps() : Cps(v.begin() + static_cast<std::ptrdiff_t>(a), v.begin() + static_cast<std::ptrdiff_t>(b));
}

bool is_sep(uint32_t c) { return c == '\\' || c == '/'; }

size_t find_sep(const Cps& v, size_t start) {
  for (size_t i = start; i < v.size(); i++) {
    if (v[i] == '\\') return i;
  }
  return static_cast<size_t>(-1);
}

void nt_splitroot(const Cps& p, Cps* drive, Cps* root, Cps* tail) {
  Cps norm = p;
  for (uint32_t& c : norm) {
    if (c == '/') c = '\\';
  }
  if (!norm.empty() && norm[0] == '\\') {
    if (norm.size() > 1 && norm[1] == '\\') {
      static const char kUnc[] = "\\\\?\\UNC\\";
      bool unc = norm.size() >= 8;
      for (size_t i = 0; unc && i < 8; i++) {
        uint32_t c = norm[i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        unc = c == static_cast<uint8_t>(kUnc[i]);
      }
      size_t index = find_sep(norm, unc ? 8 : 2);
      size_t index2 = index == static_cast<size_t>(-1) ? index : find_sep(norm, index + 1);
      if (index2 == static_cast<size_t>(-1)) {
        *drive = p;
        root->clear();
        tail->clear();
        return;
      }
      *drive = slice(p, 0, index2);
      *root = slice(p, index2, index2 + 1);
      *tail = slice(p, index2 + 1);
      return;
    }
    drive->clear();
    *root = slice(p, 0, 1);
    *tail = slice(p, 1);
    return;
  }
  if (norm.size() > 1 && norm[1] == ':') {
    *drive = slice(p, 0, 2);
    if (norm.size() > 2 && norm[2] == '\\') {
      *root = slice(p, 2, 3);
      *tail = slice(p, 3);
    } else {
      root->clear();
      *tail = slice(p, 2);
    }
    return;
  }
  drive->clear();
  root->clear();
  *tail = p;
}

}

void NtPaths::splitroot(std::string_view p, std::string* drive, std::string* root, std::string* tail) const {
  Cps d, r, t;
  nt_splitroot(code_points(p), &d, &r, &t);
  *drive = from_code_points(d);
  *root = from_code_points(r);
  *tail = from_code_points(t);
}

bool NtPaths::isabs(std::string_view p) const {
  Cps s = slice(code_points(p), 0, 3);
  for (uint32_t& c : s) {
    if (c == '/') c = '\\';
  }
  return (s.size() >= 3 && s[1] == ':' && s[2] == '\\') || (s.size() >= 2 && s[0] == '\\' && s[1] == '\\');
}

std::string NtPaths::join(std::string_view a, std::string_view b) const {
  Cps rd, rr, rp, pd, pr, pp;
  nt_splitroot(code_points(a), &rd, &rr, &rp);
  nt_splitroot(code_points(b), &pd, &pr, &pp);
  bool joined = true;
  if (!pr.empty()) {
    if (!pd.empty() || rd.empty()) rd = pd;
    rr = pr;
    rp = pp;
    joined = false;
  } else if (!pd.empty() && pd != rd) {
    if (lower(from_code_points(pd)) != lower(from_code_points(rd))) {
      rd = pd;
      rr = pr;
      rp = pp;
      joined = false;
    } else {
      rd = pd;
    }
  }
  if (joined) {
    if (!rp.empty() && !is_sep(rp.back())) rp.push_back('\\');
    rp.insert(rp.end(), pp.begin(), pp.end());
  }
  if (!rp.empty() && rr.empty() && !rd.empty() && rd.back() != ':' && !is_sep(rd.back())) {
    return from_code_points(rd) + "\\" + from_code_points(rp);
  }
  return from_code_points(rd) + from_code_points(rr) + from_code_points(rp);
}

std::pair<std::string, std::string> NtPaths::split(std::string_view p) const {
  Cps d, r, rest;
  nt_splitroot(code_points(p), &d, &r, &rest);
  size_t i = rest.size();
  while (i && !is_sep(rest[i - 1])) i--;
  Cps head = slice(rest, 0, i), tail = slice(rest, i);
  while (!head.empty() && is_sep(head.back())) head.pop_back();
  return {from_code_points(d) + from_code_points(r) + from_code_points(head), from_code_points(tail)};
}

const PathRules& native_paths() {
#if defined(_WIN32)
  static const NtPaths kPaths;
#else
  static const PosixPaths kPaths;
#endif
  return kPaths;
}

}
}

namespace tcr {
namespace py {

int compare_int_texts(std::string_view a, std::string_view b) {
  bool na = !a.empty() && a[0] == '-';
  bool nb = !b.empty() && b[0] == '-';
  if (na != nb) return na ? -1 : 1;
  std::string_view ma = na ? a.substr(1) : a;
  std::string_view mb = nb ? b.substr(1) : b;
  int c = 0;
  if (ma.size() != mb.size()) {
    c = ma.size() < mb.size() ? -1 : 1;
  } else {
    int k = ma.compare(mb);
    c = k < 0 ? -1 : k > 0 ? 1 : 0;
  }
  return na ? -c : c;
}

namespace {

}

std::optional<std::string> float_to_int_text(double f, std::string* error_type, std::string* error) {
  if (std::isnan(f)) {
    if (error_type) *error_type = "ValueError";
    if (error) *error = "cannot convert float NaN to integer";
    return std::nullopt;
  }
  if (std::isinf(f)) {
    if (error_type) *error_type = "OverflowError";
    if (error) *error = "cannot convert float infinity to integer";
    return std::nullopt;
  }
  double t = std::trunc(f);
  if (t > -9223372036854775808.0 && t < 9223372036854775808.0) return int_to_text(static_cast<int64_t>(t));
  int exp = 0;
  double frac = std::frexp(std::fabs(t), &exp);
  uint64_t mant = static_cast<uint64_t>(std::ldexp(frac, 53));
  int shift = exp - 53;
  std::vector<uint32_t> limbs;
  while (mant) {
    limbs.push_back(static_cast<uint32_t>(mant % 1000000000u));
    mant /= 1000000000u;
  }
  for (int i = 0; i < shift; i++) {
    uint32_t carry = 0;
    for (uint32_t& limb : limbs) {
      uint64_t v = static_cast<uint64_t>(limb) * 2 + carry;
      limb = static_cast<uint32_t>(v % 1000000000u);
      carry = static_cast<uint32_t>(v / 1000000000u);
    }
    if (carry) limbs.push_back(carry);
  }
  std::string out = f < 0 ? "-" : "";
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u", limbs.back());
  out += buf;
  for (size_t i = limbs.size() - 1; i-- > 0;) {
    std::snprintf(buf, sizeof(buf), "%09u", limbs[i]);
    out += buf;
  }
  return out;
}

std::optional<int> compare_int_float(std::string_view int_text, double f) {
  if (std::isnan(f)) return std::nullopt;
  if (std::isinf(f)) return f > 0 ? -1 : 1;
  if (f > -9223372036854775808.0 && f < 9223372036854775808.0) {
    std::optional<int64_t> i = int_text_to_i64(int_text);
    if (!i) return (!int_text.empty() && int_text[0] == '-') ? -1 : 1;
    double t = std::trunc(f);
    int64_t ti = static_cast<int64_t>(t);
    if (*i < ti) return -1;
    if (*i > ti) return 1;
    double frac = f - t;
    return frac > 0 ? -1 : frac < 0 ? 1 : 0;
  }
  return compare_int_texts(int_text, *float_to_int_text(f));
}

}
}
