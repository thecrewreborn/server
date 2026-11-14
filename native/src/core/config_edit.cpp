#include "core/config_edit.h"

#include <vector>

namespace tcr {
namespace config {

namespace {

struct Line {
  std::string text;
  std::string end;
};

std::vector<Line> split_lines(const std::string& text) {
  std::vector<Line> out;
  size_t at = 0;
  while (at < text.size()) {
    size_t nl = text.find('\n', at);
    Line l;
    if (nl == std::string::npos) {
      l.text = text.substr(at);
      at = text.size();
    } else {
      size_t end = nl;
      if (end > at && text[end - 1] == '\r') --end;
      l.text = text.substr(at, end - at);
      l.end = text.substr(end, nl + 1 - end);
      at = nl + 1;
    }
    out.push_back(std::move(l));
  }
  return out;
}

std::string strip(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\f\v\r");
  if (a == std::string::npos) return std::string();
  size_t b = s.find_last_not_of(" \t\f\v\r");
  return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

bool blank(const std::string& s) { return strip(s).empty(); }

bool comment(const std::string& s) {
  std::string t = strip(s);
  return !t.empty() && (t[0] == '#' || t[0] == ';');
}

bool indented(const std::string& s) { return !s.empty() && (s[0] == ' ' || s[0] == '\t'); }

bool header(const std::string& s, std::string* name) {
  std::string t = strip(s);
  if (indented(s) || t.size() < 2 || t[0] != '[') return false;
  size_t close = t.find(']');
  if (close == std::string::npos) return false;
  *name = t.substr(1, close - 1);
  return true;
}

std::string key_of(const std::string& s) {
  if (indented(s) || blank(s) || comment(s)) return std::string();
  size_t d = s.find_first_of("=:");
  if (d == std::string::npos) return std::string();
  return strip(s.substr(0, d));
}

}

bool value_ok(const std::string& value) {
  return value.find_first_of(std::string("\r\n\0", 3)) == std::string::npos;
}

std::string set_value(const std::string& text, const std::string& section, const std::string& key,
                      const std::string& value) {
  std::vector<Line> lines = split_lines(text);
  const std::string nl = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  const std::string want = lower(strip(key));
  auto line_for = [&](const std::string& k) { return value.empty() ? k + " =" : k + " = " + value; };

  bool in_section = false, found_section = false;
  size_t section_last = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string name;
    if (header(lines[i].text, &name)) {
      in_section = name == section;
      if (in_section) {
        found_section = true;
        section_last = i;
      }
      continue;
    }
    if (!in_section) continue;
    if (!blank(lines[i].text)) section_last = i;
    std::string k = key_of(lines[i].text);
    if (k.empty() || lower(k) != want) continue;
    size_t j = i + 1;
    while (j < lines.size() && indented(lines[j].text) && !blank(lines[j].text)) ++j;
    lines[i].text = line_for(k);
    if (j - 1 > i) {
      lines[i].end = lines[j - 1].end;
      lines.erase(lines.begin() + static_cast<long>(i) + 1, lines.begin() + static_cast<long>(j));
    }
    std::string out;
    for (const Line& l : lines) out += l.text + l.end;
    return out;
  }

  if (found_section) {
    Line added{line_for(want), nl};
    Line& before = lines[section_last];
    if (before.end.empty()) before.end = nl;
    lines.insert(lines.begin() + static_cast<long>(section_last) + 1, added);
    std::string out;
    for (const Line& l : lines) out += l.text + l.end;
    return out;
  }

  std::string out = text;
  if (!out.empty() && out.back() != '\n') out += nl;
  if (!out.empty() && !blank(lines.back().text)) out += nl;
  out += "[" + section + "]" + nl + line_for(want) + nl;
  return out;
}

std::string stamp_of(const FileStat& st) {
  return std::to_string(st.mtime_ns) + " " + std::to_string(st.size) + " " + std::to_string(st.ino);
}

bool read_with_stamp(Files& files, const std::string& path, std::string* text, std::string* stamp, OsError* err) {
  text->clear();
  stamp->clear();
  if (!files.exists(path) && !files.lexists(path)) return true;
  FileStat st;
  if (!files.read_stat(path, text, &st, err)) return false;
  *stamp = stamp_of(st);
  return true;
}

SaveOutcome save_value(Files& files, const std::string& path, const std::string& expected_stamp,
                       const std::string& section, const std::string& key, const std::string& value,
                       std::string* new_stamp, OsError* err) {
  std::string text, stamp;
  FileStat link;
  OsError lerr;
  bool there = files.lstat(path, &link, &lerr);
  if (there && (link.mode & 0xF000) == 0xA000) return SaveOutcome::kLink;
  if (!read_with_stamp(files, path, &text, &stamp, err)) return SaveOutcome::kFailed;
  if (stamp != expected_stamp) return SaveOutcome::kChanged;
  FileStat st;
  const uint32_t mode = there ? (link.mode & 07777) : 0644;
  if (!files.write_atomic_mode(path, set_value(text, section, key, value), mode, &st, err)) {
    return SaveOutcome::kFailed;
  }
  *new_stamp = stamp_of(st);
  return SaveOutcome::kSaved;
}

}
}
