#include "core/logtext.h"

#include "core/json.h"
#include "core/util.h"

#include <string>
#include <vector>

namespace tcr {

namespace {

bool one_code_point(std::string_view s, size_t i, size_t len, uint32_t* cp) {
  if (i + len > s.size()) return false;
  std::string text;
  py::DecodeError err;
  if (!py::decode_utf8(reinterpret_cast<const uint8_t*>(s.data() + i), len, false, &text, &err)) return false;
  std::vector<uint32_t> cps = py::code_points(text);
  if (cps.size() != 1) return false;
  *cp = cps[0];
  return true;
}

std::vector<uint32_t> unquote_plus_latin1(std::string_view name) {
  std::string s(name);
  for (char& c : s) {
    if (c == '+') c = ' ';
  }
  return py::code_points(py::unquote(latin1_to_utf8(s)));
}

bool has_secret_word(const std::vector<uint32_t>& cps) {
  std::string folded;
  for (uint32_t cp : cps) {
    char c = '\0';
    if (cp < 0x80) {
      c = static_cast<char>(cp);
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    } else if (cp == 0x212a) {
      c = 'k';
    } else if (cp == 0x17f) {
      c = 's';
    } else if (cp == 0x130 || cp == 0x131) {
      c = 'i';
    }
    folded += c;
  }
  for (const char* word : {"ticket", "token", "key", "pass"}) {
    if (folded.find(word) != std::string::npos) return true;
  }
  return false;
}

}

bool is_space_latin1(uint8_t c) {
  return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0;
}

bool is_printable_latin1(uint8_t c) {
  if (c >= 0x20 && c <= 0x7e) return true;
  return c >= 0xa1 && c != 0xad;
}

std::vector<std::string> split_ws_latin1(std::string_view s) {
  std::vector<std::string> words;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && is_space_latin1(static_cast<uint8_t>(s[i]))) ++i;
    size_t start = i;
    while (i < s.size() && !is_space_latin1(static_cast<uint8_t>(s[i]))) ++i;
    if (i > start) words.emplace_back(s.substr(start, i - start));
  }
  return words;
}

std::vector<std::string> split_ws1_latin1(std::string_view s) {
  std::vector<std::string> parts;
  size_t i = 0;
  while (i < s.size() && is_space_latin1(static_cast<uint8_t>(s[i]))) ++i;
  if (i == s.size()) return parts;
  size_t start = i;
  while (i < s.size() && !is_space_latin1(static_cast<uint8_t>(s[i]))) ++i;
  parts.emplace_back(s.substr(start, i - start));
  while (i < s.size() && is_space_latin1(static_cast<uint8_t>(s[i]))) ++i;
  if (i < s.size()) parts.emplace_back(s.substr(i));
  return parts;
}

std::string_view strip_ws_latin1(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && is_space_latin1(static_cast<uint8_t>(s[a]))) ++a;
  while (b > a && is_space_latin1(static_cast<uint8_t>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string repr_latin1(std::string_view s) {
  bool has_single = s.find('\'') != std::string_view::npos;
  bool has_double = s.find('"') != std::string_view::npos;
  char quote = (has_single && !has_double) ? '"' : '\'';
  std::string out(1, quote);
  static const char kHex[] = "0123456789abcdef";
  for (char ch : s) {
    uint8_t c = static_cast<uint8_t>(ch);
    if (ch == quote || ch == '\\') {
      out += '\\';
      out += ch;
    } else if (ch == '\t') {
      out += "\\t";
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else if (!is_printable_latin1(c)) {
      out += "\\x";
      out += kHex[c >> 4];
      out += kHex[c & 15];
    } else {
      out += ch;
    }
  }
  out += quote;
  return out;
}

std::string repr_utf8(std::string_view s) {
  bool has_single = s.find('\'') != std::string_view::npos;
  bool has_double = s.find('"') != std::string_view::npos;
  char quote = (has_single && !has_double) ? '"' : '\'';
  std::string out(1, quote);
  static const char kHex[] = "0123456789abcdef";
  size_t i = 0;
  while (i < s.size()) {
    uint8_t c = static_cast<uint8_t>(s[i]);
    size_t len = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
    uint32_t cp = c;
    bool ok = len == 1 || one_code_point(s, i, len, &cp);
    if (!ok) {
      out += "\\udc";
      out += kHex[c >> 4];
      out += kHex[c & 15];
      i += 1;
      continue;
    }
    i += len;
    if (cp == static_cast<uint8_t>(quote) || cp == '\\') {
      out += '\\';
      out += static_cast<char>(cp);
    } else if (cp == '\t') {
      out += "\\t";
    } else if (cp == '\n') {
      out += "\\n";
    } else if (cp == '\r') {
      out += "\\r";
    } else if (cp < 0x100 && !is_printable_latin1(static_cast<uint8_t>(cp))) {
      out += "\\x";
      out += kHex[cp >> 4];
      out += kHex[cp & 15];
    } else {
      out.append(s.substr(i - len, len));
    }
  }
  out += quote;
  return out;
}

std::string latin1_to_utf8(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    uint8_t c = static_cast<uint8_t>(ch);
    if (c < 0x80) {
      out += ch;
    } else {
      out += static_cast<char>(0xc0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3f));
    }
  }
  return out;
}

std::string printable(std::string_view s, size_t limit) {
  std::string cut(s.substr(0, limit));
  for (char& ch : cut) {
    if (!is_printable_latin1(static_cast<uint8_t>(ch))) ch = '?';
  }
  return latin1_to_utf8(cut);
}

std::string printable_text(std::string_view s, size_t limit) {
  std::vector<uint32_t> cps = py::code_points(s);
  if (cps.size() > limit) cps.resize(limit);
  for (uint32_t& cp : cps) {
    if (!py::isprintable(cp)) cp = '?';
  }
  return py::from_code_points(cps);
}

std::string json_quote_latin1(std::string_view s) { return json::dumps(json::Value::string(latin1_to_utf8(s))); }

std::string json_quote_utf8(std::string_view s) {
  std::string text;
  size_t i = 0;
  while (i < s.size()) {
    uint8_t c = static_cast<uint8_t>(s[i]);
    size_t len = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
    uint32_t cp = c;
    if (len > 1 && one_code_point(s, i, len, &cp)) {
      text.append(s.substr(i, len));
      i += len;
    } else {
      py::append_utf8(text, c);
      i += 1;
    }
  }
  return json::dumps(json::Value::string(text));
}

std::optional<AuthTicket> auth_ticket(std::string_view auth) {
  for (size_t i = 0; i + 1 < auth.size(); ++i) {
    if ((auth[i] != 't' && auth[i] != 'T') || auth[i + 1] != '=') continue;
    if (i > 0) {
      uint8_t before = static_cast<uint8_t>(auth[i - 1]);
      if (!is_space_latin1(before) && before != ',' && before != ';') continue;
    }
    AuthTicket t;
    std::vector<std::string> words = split_ws_latin1(auth.substr(0, i));
    t.scheme = words.empty() ? std::string() : words[0];
    t.key = std::string(auth.substr(i, 2));
    t.ticket = std::string(auth.substr(i + 2));
    return t;
  }
  return std::nullopt;
}

std::string redact(std::string_view auth) {
  if (strip_ws_latin1(auth).empty()) return std::string(auth);
  std::optional<AuthTicket> t = auth_ticket(auth);
  if (t) {
    std::string prefix = t->scheme.empty() ? t->key : t->scheme + " " + t->key;
    std::string lower = t->scheme;
    for (char& c : lower) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    size_t bar = t->ticket.find('|');
    if (lower != "psn2.0" && bar != std::string::npos) return prefix + t->ticket.substr(0, bar) + "|***";
    return prefix + "***";
  }
  std::vector<std::string> parts = split_ws1_latin1(auth);
  return parts.size() == 2 ? parts[0] + " ***" : "***";
}

std::string log_path(std::string_view path) {
  size_t q = path.find('?');
  if (q == std::string_view::npos) return std::string(path);
  std::string_view base = path.substr(0, q);
  std::string_view query = path.substr(q + 1);
  std::string out(base);
  out += '?';
  size_t start = 0;
  bool first = true;
  for (;;) {
    size_t amp = query.find('&', start);
    std::string_view part = query.substr(start, amp == std::string_view::npos ? std::string_view::npos : amp - start);
    std::string_view name = part.substr(0, part.find('='));
    if (!first) out += '&';
    first = false;
    if (has_secret_word(unquote_plus_latin1(name))) {
      out += std::string(name) + "=***";
    } else {
      out += std::string(part);
    }
    if (amp == std::string_view::npos) break;
    start = amp + 1;
  }
  return out;
}

std::string log_body(std::string_view path, std::string_view raw) {
  if (raw.empty()) return "";
  std::string_view base = path.substr(0, path.find('?'));
  if (base.size() >= 5 && base.substr(base.size() - 5) == ".rgba") {
    return "<rgba " + std::to_string(raw.size()) + " bytes>";
  }
  std::string text;
  py::DecodeError err;
  bool binary = !py::decode_utf8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), false, &text, &err);
  for (char ch : text) {
    uint8_t cp = static_cast<uint8_t>(ch);
    if (cp < 0x20 && cp != '\t' && cp != '\r' && cp != '\n') binary = true;
  }
  if (binary) return "<binary " + std::to_string(raw.size()) + " bytes>";
  if (raw.size() <= kLogBody) return std::string(raw);
  std::string_view cut = raw.substr(0, kLogBody);
  size_t end = cut.size();
  size_t lead = end;
  while (lead > 0 && (static_cast<uint8_t>(cut[lead - 1]) & 0xc0) == 0x80) --lead;
  if (lead > 0) {
    uint8_t c = static_cast<uint8_t>(cut[lead - 1]);
    size_t len = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
    if (end - (lead - 1) < len) end = lead - 1;
  }
  return std::string(cut.substr(0, end)) + "... <" + std::to_string(raw.size()) + " bytes>";
}

}
