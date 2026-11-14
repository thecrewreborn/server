#include <cerrno>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "app/args.h"
#include "app/server.h"
#include "app/system.h"
#include "core/accounts.h"
#include "core/json.h"
#include "core/pyobj.h"
#include "core/util.h"
#include "platform/files.h"
#include "platform/platform.h"

namespace tcr::app {

namespace {

const char kProg[] = "TheCrewReborn-Server accounts";
const char kUsage[] = "usage: TheCrewReborn-Server accounts [-h] [--file FILE] {list,link,free} ...\n";

std::string wrap(const std::string& text, size_t width, size_t indent) {
  std::vector<std::string> chunks;
  std::string word;
  auto flush = [&]() {
    if (!word.empty()) chunks.push_back(word);
    word.clear();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == ' ') {
      flush();
      continue;
    }
    word += c;
    bool inner_hyphen = c == '-' && i > 0 && std::isalnum(static_cast<unsigned char>(text[i - 1])) &&
                        i + 1 < text.size() && std::isalnum(static_cast<unsigned char>(text[i + 1]));
    if (inner_hyphen) {
      chunks.push_back(word + "\x01");
      word.clear();
    }
  }
  flush();
  std::vector<std::string> lines;
  std::string line;
  size_t line_len = 0;
  bool glued = false;
  for (std::string chunk : chunks) {
    bool hyphen_end = !chunk.empty() && chunk.back() == '\x01';
    if (hyphen_end) chunk.pop_back();
    size_t need = chunk.size() + (line.empty() || glued ? 0 : 1);
    if (!line.empty() && line_len + need > width) {
      lines.push_back(line);
      line.clear();
      line_len = 0;
      need = chunk.size();
    }
    while (line.empty() && chunk.size() > width) {
      lines.push_back(chunk.substr(0, width));
      chunk = chunk.substr(width);
      need = chunk.size();
    }
    if (!line.empty() && !glued) {
      line += ' ';
      ++line_len;
    }
    line += chunk;
    line_len += chunk.size();
    glued = hyphen_end;
  }
  if (!line.empty()) lines.push_back(line);
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i) out += "\n" + std::string(indent, ' ');
    out += lines[i];
  }
  return out;
}

std::string main_help(const std::string& file) {
  return std::string(kUsage) +
         "\n"
         "List, link or free player-name claims.\n"
         "\n"
         "positional arguments:\n"
         "  {list,link,free}\n"
         "    list            print the claimed names\n"
         "    link            let one new device log in with a claimed name for a few\n"
         "                    minutes\n"
         "    free            free a claim\n"
         "\n"
         "options:\n"
         "  -h, --help        show this help message and exit\n"
         "  --file FILE       " +
         wrap("accounts file (default: " + file + ")", 78 - 20, 20) + "\n";
}

const ParserSpec& list_parser() {
  static const ParserSpec spec = [] {
    ParserSpec p;
    p.prog = std::string(kProg) + " list";
    p.usage = "usage: TheCrewReborn-Server accounts list [-h]\n";
    p.help = p.usage + "\noptions:\n  -h, --help  show this help message and exit\n";
    p.options = {{{"-h", "--help"}, "help", OptionSpec::kHelp, OptionSpec::kText}};
    p.subcommand = true;
    return p;
  }();
  return spec;
}

const ParserSpec& link_parser() {
  static const ParserSpec spec = [] {
    ParserSpec p;
    p.prog = std::string(kProg) + " link";
    p.usage = "usage: TheCrewReborn-Server accounts link [-h] [--minutes MINUTES] name\n";
    p.help = p.usage +
             "\npositional arguments:\n  name\n\noptions:\n  -h, --help         show this help message and exit\n"
             "  --minutes MINUTES  how long (default: 10)\n";
    p.options = {{{"-h", "--help"}, "help", OptionSpec::kHelp, OptionSpec::kText},
                 {{"--minutes"}, "minutes", OptionSpec::kValue, OptionSpec::kFloat}};
    p.positionals = {"name"};
    p.subcommand = true;
    return p;
  }();
  return spec;
}

const ParserSpec& free_parser() {
  static const ParserSpec spec = [] {
    ParserSpec p;
    p.prog = std::string(kProg) + " free";
    p.usage = "usage: TheCrewReborn-Server accounts free [-h] name\n";
    p.help = p.usage + "\npositional arguments:\n  name\n\noptions:\n  -h, --help  show this help message and exit\n";
    p.options = {{{"-h", "--help"}, "help", OptionSpec::kHelp, OptionSpec::kText}};
    p.positionals = {"name"};
    p.subcommand = true;
    return p;
  }();
  return spec;
}

std::string text_of(const json::Value& v) { return obj::str(v); }

bool from_timestamp(const json::Value& v, LocalTime* out, Raised* raised) {
  int64_t sec = 0;
  int us = 0;
  if (v.is_float()) {
    if (std::isnan(v.as_float())) {
      *raised = Raised{"ValueError", "Invalid value NaN (not a number)"};
      return false;
    }
    if (!split_timestamp(v.as_float(), &sec, &us)) {
      *raised = Raised{"OverflowError", "timestamp out of range for platform time_t"};
      return false;
    }
  } else if (v.is_int() || v.is_bool()) {
    std::optional<int64_t> n = v.is_bool() ? std::optional<int64_t>(v.as_bool()) : v.as_i64();
    if (!n) {
      *raised = Raised{"OverflowError", "timestamp out of range for platform time_t"};
      return false;
    }
    sec = *n;
  } else {
    *raised = Raised{"TypeError", std::string("'") + obj::type_name(v) + "' object cannot be interpreted as an integer"};
    return false;
  }
  auto local_year = [&](int64_t when, std::tm* tm) {
    std::time_t t = static_cast<std::time_t>(when);
    errno = 0;
#if defined(_WIN32)
    int failed = localtime_s(tm, &t);
    if (failed) errno = failed;
#else
    bool failed = localtime_r(&t, tm) == nullptr;
#endif
    if (failed) {
      int code = errno ? errno : EINVAL;
      *raised = Raised{"OSError", plat::python_error_text(plat::system_error(code))};
      return false;
    }
    int y = tm->tm_year + 1900;
    if (y < 1 || y > 9999) {
      *raised = Raised{"ValueError", "year " + std::to_string(y) + " is out of range"};
      return false;
    }
    return true;
  };
  std::tm tm{};
  if (!local_year(sec, &tm)) return false;
#if !defined(_WIN32)
  std::tm probe{};
  if (!local_year(sec - 24 * 3600, &probe)) return false;
#endif
  int year = tm.tm_year + 1900;
  out->year = year;
  out->month = tm.tm_mon + 1;
  out->day = tm.tm_mday;
  out->hour = tm.tm_hour;
  out->minute = tm.tm_min;
  out->second = tm.tm_sec;
  out->microsecond = us;
  return true;
}

bool truthy(const json::Value& v) {
  if (v.is_null()) return false;
  if (v.is_bool()) return v.as_bool();
  if (v.is_int()) return v.int_text() != "0";
  if (v.is_float()) return v.as_float() != 0.0;
  if (v.is_string()) return !v.as_string().empty();
  return v.size() != 0;
}

bool owner_of(const std::string& path, int64_t* uid, int64_t* gid) {
#if defined(_WIN32)
  (void)path;
  (void)uid;
  (void)gid;
  return false;
#else
  struct stat st;
  if (::lstat(path.c_str(), &st) == 0 && !S_ISLNK(st.st_mode)) {
    *uid = st.st_uid;
    *gid = st.st_gid;
    return true;
  }
  std::string abs;
  plat::Error err;
  if (!plat::abs_path(path, &abs, &err)) return false;
  std::string folder = plat::dir_name(abs);
  if (::lstat(folder.c_str(), &st) == 0) {
    *uid = st.st_uid;
    *gid = st.st_gid;
    return true;
  }
  return false;
#endif
}

bool run_as_root() {
#if defined(_WIN32)
  return false;
#else
  return ::geteuid() == 0;
#endif
}

int crash(const Raised& r) {
  say_error("Traceback (most recent call last):");
  say_error(r.line());
  return 1;
}

}

int run_accounts(const std::vector<std::string>& argv) {
  const std::string home = home_folder();
  const std::string default_file = plat::join_path(plat::join_path(home, "save"), kAccountsStoreName);
  ParserSpec spec;
  spec.prog = kProg;
  spec.usage = kUsage;
  spec.help = main_help(default_file);
  spec.options = {{{"-h", "--help"}, "help", OptionSpec::kHelp, OptionSpec::kText},
                  {{"--file"}, "file", OptionSpec::kValue, OptionSpec::kText}};
  spec.command_dest = "cmd";
  spec.commands = {{"list", &list_parser()}, {"link", &link_parser()}, {"free", &free_parser()}};
  int code = 0;
  std::optional<Parsed> args = parse_args(spec, argv, &code);
  if (!args) return code;
  const std::string file = args->get("file", default_file);

  platform::SystemFiles files;
  SystemEnv env(files, config_file(home));
  std::array<uint8_t, 32> key{};
  env.urandom(key.data(), key.size());
  int64_t uid = -1, gid = -1;
  if (run_as_root()) owner_of(file, &uid, &gid);
  AccountStore store(env, files, file, key, uid, gid);
  Raised raised;

  if (args->command == "list") {
    json::Value entries = store.entries(&raised);
    if (!raised.type.empty()) return crash(raised);
    for (const json::Member& m : entries.members()) {
      const json::Value& e = m.value;
      std::string day = "?";
      if (truthy(*e.get("created"))) {
        LocalTime t;
        if (!from_timestamp(*e.get("created"), &t, &raised)) return crash(raised);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.year, t.month, t.day);
        day = buf;
      }
      std::string devices = text_of(*e.get("devices")) + " device" + (text_of(*e.get("devices")) == "1" ? "" : "s");
      std::string window;
      if (truthy(*e.get("link"))) {
        LocalTime t;
        if (!from_timestamp(*e.get("link"), &t, &raised)) return crash(raised);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
        window = std::string("  link open until ") + buf;
      }
      say(text_of(*e.get("name")) + "  " + text_of(*e.get("platform")) + "  " + day + "  " + devices + window);
    }
    return 0;
  }
  if (args->command == "link") {
    std::string name = args->get("name");
    double minutes = kLinkMinutes;
    if (args->has("minutes")) minutes = *py::parse_float(args->get("minutes"));
    if (!(0 < minutes && minutes <= 7 * 24 * 60)) {
      return arg_error(spec, "--minutes must be more than 0 and at most a week");
    }
    std::optional<std::string> display = store.display_name(name, &raised);
    if (!raised.type.empty()) return crash(raised);
    if (!display) {
      say("no claim for " + name);
      return 1;
    }
    std::optional<bool> opened = store.open_link(name, minutes, &raised);
    if (!opened) return crash(raised);
    if (!*opened) {
      say("could not write " + file);
      return 1;
    }
    display = store.display_name(name, &raised);
    if (!raised.type.empty()) return crash(raised);
    char g[64];
    std::snprintf(g, sizeof(g), "%g", minutes);
    say("link window for " + display.value_or("None") + " open for " + g +
        " minutes: log in with the new device now");
    return 0;
  }
  std::string name = args->get("name");
  bool freed = store.free(name, &raised);
  if (!raised.type.empty()) return crash(raised);
  say(freed ? "freed " + name : "no claim for " + name);
  return 0;
}

}
