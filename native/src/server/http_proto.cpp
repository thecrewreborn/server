#include "server/http_proto.h"

#include <cmath>
#include <cstdio>

namespace tcr::http {

namespace {

struct StatusRow {
  int code;
  const char* phrase;
  const char* explain;
};

constexpr StatusRow kStatus[] = {
    {100, "Continue", "Request received, please continue"},
    {101, "Switching Protocols", "Switching to new protocol; obey Upgrade header"},
    {102, "Processing", ""},
    {103, "Early Hints", ""},
    {200, "OK", "Request fulfilled, document follows"},
    {201, "Created", "Document created, URL follows"},
    {202, "Accepted", "Request accepted, processing continues off-line"},
    {203, "Non-Authoritative Information", "Request fulfilled from cache"},
    {204, "No Content", "Request fulfilled, nothing follows"},
    {205, "Reset Content", "Clear input form for further input"},
    {206, "Partial Content", "Partial content follows"},
    {207, "Multi-Status", ""},
    {208, "Already Reported", ""},
    {226, "IM Used", ""},
    {300, "Multiple Choices", "Object has several resources -- see URI list"},
    {301, "Moved Permanently", "Object moved permanently -- see URI list"},
    {302, "Found", "Object moved temporarily -- see URI list"},
    {303, "See Other", "Object moved -- see Method and URL list"},
    {304, "Not Modified", "Document has not changed since given time"},
    {305, "Use Proxy", "You must use proxy specified in Location to access this resource"},
    {307, "Temporary Redirect", "Object moved temporarily -- see URI list"},
    {308, "Permanent Redirect", "Object moved permanently -- see URI list"},
    {400, "Bad Request", "Bad request syntax or unsupported method"},
    {401, "Unauthorized", "No permission -- see authorization schemes"},
    {402, "Payment Required", "No payment -- see charging schemes"},
    {403, "Forbidden", "Request forbidden -- authorization will not help"},
    {404, "Not Found", "Nothing matches the given URI"},
    {405, "Method Not Allowed", "Specified method is invalid for this resource"},
    {406, "Not Acceptable", "URI not available in preferred format"},
    {407, "Proxy Authentication Required", "You must authenticate with this proxy before proceeding"},
    {408, "Request Timeout", "Request timed out; try again later"},
    {409, "Conflict", "Request conflict"},
    {410, "Gone", "URI no longer exists and has been permanently removed"},
    {411, "Length Required", "Client must specify Content-Length"},
    {412, "Precondition Failed", "Precondition in headers is false"},
    {413, "Content Too Large", "Content is too large"},
    {414, "URI Too Long", "URI is too long"},
    {415, "Unsupported Media Type", "Entity body in unsupported format"},
    {416, "Range Not Satisfiable", "Cannot satisfy request range"},
    {417, "Expectation Failed", "Expect condition could not be satisfied"},
    {418, "I'm a Teapot", "Server refuses to brew coffee because it is a teapot."},
    {421, "Misdirected Request", "Server is not able to produce a response"},
    {422, "Unprocessable Content", ""},
    {423, "Locked", ""},
    {424, "Failed Dependency", ""},
    {425, "Too Early", ""},
    {426, "Upgrade Required", ""},
    {428, "Precondition Required", "The origin server requires the request to be conditional"},
    {429, "Too Many Requests",
     "The user has sent too many requests in a given amount of time (\"rate limiting\")"},
    {431, "Request Header Fields Too Large",
     "The server is unwilling to process the request because its header fields are too large"},
    {451, "Unavailable For Legal Reasons",
     "The server is denying access to the resource as a consequence of a legal demand"},
    {500, "Internal Server Error", "Server got itself in trouble"},
    {501, "Not Implemented", "Server does not support this operation"},
    {502, "Bad Gateway", "Invalid responses from another server/proxy"},
    {503, "Service Unavailable", "The server cannot process the request due to a high load"},
    {504, "Gateway Timeout", "The gateway server did not receive a timely response"},
    {505, "HTTP Version Not Supported", "Cannot fulfill request"},
    {506, "Variant Also Negotiates", ""},
    {507, "Insufficient Storage", ""},
    {508, "Loop Detected", ""},
    {510, "Not Extended", ""},
    {511, "Network Authentication Required", "The client needs to authenticate to gain network access"},
};

bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

constexpr size_t kMaxStrDigits = 4300;

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool header_re_match(std::string_view line) {
  if (line.substr(0, 5) == "From ") return true;
  if (!line.empty() && (line[0] == '\t' || line[0] == ' ')) return true;
  for (char ch : line) {
    uint8_t c = static_cast<uint8_t>(ch);
    if (c == ':') return true;
    if (!(c >= 0x21 && c <= 0x7e)) return false;
  }
  return false;
}

std::vector<std::string_view> split_lines_keep(std::string_view s) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\n') {
      lines.push_back(s.substr(start, i + 1 - start));
      start = i + 1;
    } else if (s[i] == '\r') {
      size_t end = (i + 1 < s.size() && s[i + 1] == '\n') ? i + 2 : i + 1;
      lines.push_back(s.substr(start, end - start));
      start = end;
      i = end - 1;
    }
  }
  if (start < s.size()) lines.push_back(s.substr(start));
  return lines;
}

}

bool status_text(int code, const char** phrase, const char** explain) {
  for (const StatusRow& row : kStatus) {
    if (row.code == code) {
      *phrase = row.phrase;
      *explain = row.explain;
      return true;
    }
  }
  return false;
}

std::string html_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '&') {
      out += "&amp;";
    } else if (c == '<') {
      out += "&lt;";
    } else if (c == '>') {
      out += "&gt;";
    } else {
      out += c;
    }
  }
  return out;
}

UtcTime utc_time(double t) {
  int64_t secs = static_cast<int64_t>(std::floor(t));
  int64_t days = secs >= 0 ? secs / 86400 : -((-secs + 86399) / 86400);
  int64_t rem = secs - days * 86400;
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t d = doy - (153 * mp + 2) / 5 + 1;
  int64_t m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) ++y;
  UtcTime u;
  u.year = static_cast<int>(y);
  u.month = static_cast<int>(m);
  u.day = static_cast<int>(d);
  u.hour = static_cast<int>(rem / 3600);
  u.minute = static_cast<int>(rem / 60 % 60);
  u.second = static_cast<int>(rem % 60);
  u.weekday = static_cast<int>(((days % 7) + 7 + 3) % 7);
  return u;
}

std::string http_date(double t) {
  static const char* const kDays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  UtcTime u = utc_time(t);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT", kDays[u.weekday], u.day,
                kMonths[u.month - 1], u.year, u.hour, u.minute, u.second);
  return buf;
}

bool py_int_latin1(std::string_view text, int64_t* out) {
  std::string ascii(text);
  for (char& c : ascii) {
    uint8_t u = static_cast<uint8_t>(c);
    if (u >= 0x80 && tcr::is_space_latin1(u)) c = ' ';
  }
  auto ascii_space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
  std::string_view s(ascii);
  while (!s.empty() && ascii_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && ascii_space(s.back())) s.remove_suffix(1);
  bool negative = false;
  if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
    negative = s[0] == '-';
    s.remove_prefix(1);
  }
  if (s.empty() || !is_ascii_digit(s[0]) || !is_ascii_digit(s.back())) return false;
  uint64_t value = 0;
  bool saturated = false;
  size_t digits = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '_') {
      if (!is_ascii_digit(s[i + 1])) return false;
      continue;
    }
    if (!is_ascii_digit(c)) return false;
    if (++digits > kMaxStrDigits) return false;
    if (value > (UINT64_MAX - 9) / 10) {
      saturated = true;
    } else {
      value = value * 10 + static_cast<uint64_t>(c - '0');
    }
  }
  if (saturated || value > static_cast<uint64_t>(INT64_MAX)) value = static_cast<uint64_t>(INT64_MAX);
  *out = negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
  return true;
}

Headers Headers::parse(std::string_view block) {
  Headers h;
  std::vector<std::string_view> all = split_lines_keep(block);
  std::vector<std::string_view> lines;
  for (std::string_view line : all) {
    if (!header_re_match(line)) break;
    lines.push_back(line);
  }
  std::string last_name;
  std::vector<std::string_view> last_value;
  auto flush = [&]() {
    if (last_name.empty()) return;
    std::string_view first = last_value[0];
    size_t colon = first.find(':');
    std::string value(first.substr(colon + 1));
    for (size_t i = 1; i < last_value.size(); ++i) value.append(last_value[i]);
    size_t a = value.find_first_not_of(" \t\r\n");
    value = a == std::string::npos ? std::string() : value.substr(a);
    size_t b = value.find_last_not_of("\r\n");
    value = b == std::string::npos ? std::string() : value.substr(0, b + 1);
    h.items_.emplace_back(std::string(first.substr(0, colon)), value);
    last_name.clear();
    last_value.clear();
  };
  for (size_t lineno = 0; lineno < lines.size(); ++lineno) {
    std::string_view line = lines[lineno];
    if (line[0] == ' ' || line[0] == '\t') {
      if (last_name.empty()) continue;
      last_value.push_back(line);
      continue;
    }
    flush();
    if (line.substr(0, 5) == "From ") {
      if (lineno == 0) continue;
      if (lineno == lines.size() - 1) return h;
      continue;
    }
    size_t colon = line.find(':');
    if (colon == 0) continue;
    last_name = std::string(line.substr(0, colon));
    last_value.push_back(line);
  }
  flush();
  return h;
}

std::optional<std::string> Headers::get(std::string_view name) const {
  for (const auto& item : items_) {
    const std::string& k = item.first;
    if (k.size() != name.size()) continue;
    bool same = true;
    for (size_t i = 0; i < k.size() && same; ++i) same = ascii_lower(k[i]) == ascii_lower(name[i]);
    if (same) return item.second;
  }
  return std::nullopt;
}

Version parse_version(std::string_view word) {
  Version v;
  if (word.substr(0, 5) != "HTTP/") return v;
  std::string_view base = word.substr(5);
  v.base = std::string(base);
  size_t dot = base.find('.');
  if (dot == std::string_view::npos || base.find('.', dot + 1) != std::string_view::npos) return v;
  std::string_view parts[2] = {base.substr(0, dot), base.substr(dot + 1)};
  uint64_t numbers[2] = {0, 0};
  for (int i = 0; i < 2; ++i) {
    if (parts[i].empty() || parts[i].size() > 10) return v;
    for (char c : parts[i]) {
      if (!is_ascii_digit(c)) return v;
      numbers[i] = numbers[i] * 10 + static_cast<uint64_t>(c - '0');
    }
  }
  v.valid = true;
  v.major = numbers[0];
  v.minor = numbers[1];
  return v;
}

}
