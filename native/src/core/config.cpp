#include "core/config.h"

#include <cstdio>
#include <set>

namespace tcr {
namespace config {

namespace {

const char kDefault[] = "DEFAULT";
constexpr int kMaxInterpolationDepth = 10;

std::string optionxform(const std::string& s) { return py::lower(s); }

std::string lineno2(size_t n) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%2zu", n);
  return buf;
}

Error make(const char* type, std::string message) { return Error{type, std::move(message)}; }

std::vector<std::string> text_lines(const std::string& text, bool complete_only) {
  std::vector<std::string> lines;
  std::string cur;
  size_t n = text.size();
  for (size_t i = 0; i < n; i++) {
    char c = text[i];
    if (c == '\r') {
      if (i + 1 == n && complete_only) break;
      if (i + 1 < n && text[i + 1] == '\n') i++;
      cur += '\n';
      lines.push_back(cur);
      cur.clear();
    } else if (c == '\n') {
      cur += '\n';
      lines.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty() && !complete_only) lines.push_back(cur);
  return lines;
}

bool all_space(const std::vector<uint32_t>& cps, size_t a, size_t b) {
  for (size_t i = a; i < b; i++)
    if (!py::isspace(cps[i])) return false;
  return true;
}

}

bool Error::configparser() const {
  return type == "ParsingError" || type == "MissingSectionHeaderError" || type == "DuplicateSectionError" ||
         type == "DuplicateOptionError" || type == "NoSectionError" || type == "NoOptionError" ||
         type == "InterpolationSyntaxError" || type == "InterpolationMissingOptionError" ||
         type == "InterpolationDepthError" || type == "MultilineContinuationError";
}

Ini::Option* Ini::Section::find(const std::string& key) {
  for (Option& o : options)
    if (o.key == key) return &o;
  return nullptr;
}

const Ini::Option* Ini::Section::find(const std::string& key) const {
  for (const Option& o : options)
    if (o.key == key) return &o;
  return nullptr;
}

Ini::Section* Ini::section(const std::string& name) {
  for (Section& s : sections_)
    if (s.name == name) return &s;
  return nullptr;
}

const Ini::Section* Ini::section(const std::string& name) const {
  for (const Section& s : sections_)
    if (s.name == name) return &s;
  return nullptr;
}

bool Ini::read(const std::optional<std::string>& file, const std::string& path, Error* err, Files* files) {
  sections_.clear();
  defaults_ = Section{kDefault, {}};
  if (!file) return true;
  std::string text;
  std::optional<std::string> decode_error = decode_text_file(files, *file, false, &text);
  std::vector<std::string> lines = text_lines(text, decode_error.has_value());

  Section* cursect = nullptr;
  std::string sectname;
  std::optional<std::string> optname;
  size_t indent_level = 0;
  std::set<std::string> sections_added;
  std::set<std::pair<std::string, std::string>> options_added;
  std::vector<std::pair<size_t, std::string>> errors;
  auto value_of = [&]() -> Option* { return cursect && optname ? cursect->find(*optname) : nullptr; };

  for (size_t k = 0; k < lines.size(); k++) {
    const std::string& line = lines[k];
    const size_t lineno = k + 1;
    std::string stripped = py::strip(line);
    bool comment = !stripped.empty() && (stripped[0] == '#' || stripped[0] == ';');
    std::string clean = comment ? std::string() : stripped;
    if (clean.empty()) {
      if (!comment && cursect && optname && !optname->empty()) {
        if (Option* o = value_of()) o->lines.push_back(std::string());
      }
      continue;
    }
    std::vector<uint32_t> cps = py::code_points(line);
    size_t cur_indent = 0;
    while (cur_indent < cps.size() && py::isspace(cps[cur_indent])) cur_indent++;
    if (cur_indent == cps.size()) cur_indent = 0;

    if (cursect && optname && !optname->empty() && cur_indent > indent_level) {
      if (Option* o = value_of()) o->lines.push_back(clean);
      continue;
    }
    indent_level = cur_indent;
    std::optional<std::string> header;
    if (clean[0] == '[') {
      size_t close = clean.rfind(']');
      if (close != std::string::npos && close >= 2) header = clean.substr(1, close - 1);
    }
    if (!header && !cursect) {
      *err = make("MissingSectionHeaderError", "File contains no section headers.\nfile: " + py::repr(path) +
                                                   ", line: " + std::to_string(lineno) + "\n" + py::repr(line));
      return false;
    }
    if (header) {
      sectname = *header;
      if (section(sectname)) {
        if (sections_added.count(sectname)) {
          *err = make("DuplicateSectionError", "While reading from " + py::repr(path) + " [line " + lineno2(lineno) +
                                                   "]: section " + py::repr(sectname) + " already exists");
          return false;
        }
        cursect = section(sectname);
        sections_added.insert(sectname);
      } else if (sectname == kDefault) {
        cursect = &defaults_;
      } else {
        sections_.push_back(Section{sectname, {}});
        cursect = &sections_.back();
        sections_added.insert(sectname);
      }
      optname.reset();
      continue;
    }
    std::vector<uint32_t> ccps = py::code_points(clean);
    size_t d = 0;
    while (d < ccps.size() && ccps[d] != '=' && ccps[d] != ':') d++;
    if (d == ccps.size()) {
      errors.emplace_back(lineno, line);
      continue;
    }
    size_t oe = d;
    while (oe > 0 && all_space(ccps, oe - 1, oe)) oe--;
    std::string option = py::from_code_points(std::vector<uint32_t>(ccps.begin(), ccps.begin() + oe));
    std::string value = py::strip(py::from_code_points(std::vector<uint32_t>(ccps.begin() + d + 1, ccps.end())));
    if (option.empty()) errors.emplace_back(lineno, line);
    std::string key = optionxform(py::rstrip(option));
    optname = key;
    if (options_added.count({sectname, key})) {
      *err = make("DuplicateOptionError", "While reading from " + py::repr(path) + " [line " + lineno2(lineno) +
                                              "]: option " + py::repr(key) + " in section " + py::repr(sectname) +
                                              " already exists");
      return false;
    }
    options_added.insert({sectname, key});
    if (Option* o = cursect->find(key)) {
      o->lines = {value};
    } else {
      cursect->options.push_back(Option{key, {value}, std::string()});
    }
  }
  if (decode_error) {
    *err = make("UnicodeDecodeError", *decode_error);
    return false;
  }
  auto join = [](Section& s) {
    for (Option& o : s.options) {
      std::string v;
      for (size_t i = 0; i < o.lines.size(); i++) {
        if (i) v += '\n';
        v += o.lines[i];
      }
      o.value = py::rstrip(v);
      o.lines.clear();
    }
  };
  join(defaults_);
  for (Section& s : sections_) join(s);
  if (!errors.empty()) {
    std::string msg = "Source contains parsing errors: " + py::repr(path);
    for (const auto& e : errors) msg += "\n\t[line " + lineno2(e.first) + "]: " + py::repr(e.second);
    *err = make("ParsingError", msg);
    return false;
  }
  return true;
}

bool Ini::has_section(const std::string& name) const { return section(name) != nullptr; }

bool Ini::has_option(const std::string& sect, const std::string& option) const {
  std::string key = optionxform(option);
  if (sect.empty() || sect == kDefault) return defaults_.find(key) != nullptr;
  const Section* s = section(sect);
  if (!s) return false;
  return s->find(key) != nullptr || defaults_.find(key) != nullptr;
}

const std::string* Ini::lookup(const Section* sect, const std::string& option) const {
  if (sect) {
    if (const Option* o = sect->find(option)) return &o->value;
  }
  if (const Option* o = defaults_.find(option)) return &o->value;
  return nullptr;
}

bool Ini::interpolate(const std::string& sect_name, const std::string& option, const std::string& rawval,
                      const Section* sect, std::string& accum, const std::string& rest_in, int depth,
                      Error* err) const {
  if (depth > kMaxInterpolationDepth) {
    *err = make("InterpolationDepthError",
                "Recursion limit exceeded in value substitution: option " + py::repr(option) + " in section " +
                    py::repr(sect_name) + " contains an interpolation key which cannot be substituted in " +
                    std::to_string(kMaxInterpolationDepth) + " steps. Raw value: " + py::repr(rawval));
    return false;
  }
  std::string rest = rest_in;
  while (!rest.empty()) {
    size_t p = rest.find('%');
    if (p == std::string::npos) {
      accum += rest;
      return true;
    }
    if (p > 0) {
      accum += rest.substr(0, p);
      rest = rest.substr(p);
    }
    char c = rest.size() > 1 ? rest[1] : '\0';
    if (rest.size() > 1 && c == '%') {
      accum += '%';
      rest = rest.substr(2);
    } else if (rest.size() > 1 && c == '(') {
      size_t close = rest.find(')', 2);
      if (close == std::string::npos || close == 2 || close + 1 >= rest.size() || rest[close + 1] != 's') {
        *err = make("InterpolationSyntaxError", "bad interpolation variable reference " + py::repr(rest));
        return false;
      }
      std::string var = optionxform(rest.substr(2, close - 2));
      rest = rest.substr(close + 2);
      const std::string* v = lookup(sect, var);
      if (!v) {
        *err = make("InterpolationMissingOptionError",
                    "Bad value substitution: option " + py::repr(option) + " in section " + py::repr(sect_name) +
                        " contains an interpolation key " + py::repr(var) +
                        " which is not a valid option name. Raw value: " + py::repr(rawval));
        return false;
      }
      if (v->find('%') != std::string::npos) {
        if (!interpolate(sect_name, option, rawval, sect, accum, *v, depth + 1, err)) return false;
      } else {
        accum += *v;
      }
    } else {
      *err = make("InterpolationSyntaxError", "'%' must be followed by '%' or '(', found: " + py::repr(rest));
      return false;
    }
  }
  return true;
}

Result<std::string> Ini::get(const std::string& sect, const std::string& option) const {
  const Section* s = section(sect);
  if (!s && sect != kDefault) return Result<std::string>::fail(make("NoSectionError", "No section: " + py::repr(sect)));
  std::string key = optionxform(option);
  const std::string* raw = lookup(s, key);
  if (!raw) {
    return Result<std::string>::fail(
        make("NoOptionError", "No option " + py::repr(key) + " in section: " + py::repr(sect)));
  }
  std::string accum;
  Error err;
  if (!interpolate(sect, key, *raw, s, accum, *raw, 1, &err)) return Result<std::string>::fail(err);
  return Result<std::string>::of(accum);
}

Result<std::string> Ini::get(const std::string& sect, const std::string& option, const std::string& fallback) const {
  Result<std::string> r = get(sect, option);
  if (!r.ok() && (r.error.type == "NoSectionError" || r.error.type == "NoOptionError")) {
    return Result<std::string>::of(fallback);
  }
  return r;
}

Result<std::string> Ini::getint(const std::string& sect, const std::string& option, int64_t fallback) const {
  Result<std::string> r = get(sect, option);
  if (!r.ok()) {
    if (r.error.type == "NoSectionError" || r.error.type == "NoOptionError") {
      return Result<std::string>::of(py::int_to_text(fallback));
    }
    return r;
  }
  std::string error;
  std::optional<std::string> v = py::parse_int(*r.value, &error);
  if (!v) return Result<std::string>::fail(make("ValueError", error));
  return Result<std::string>::of(*v);
}

bool is_ipv4(const std::string& value) {
  if (value.empty()) return false;
  if (value.find('/') != std::string::npos) return false;
  size_t start = 0;
  int octets = 0;
  while (true) {
    size_t dot = value.find('.', start);
    std::string part = value.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (part.empty() || part.size() > 3) return false;
    for (char c : part)
      if (c < '0' || c > '9') return false;
    if (part.size() > 1 && part[0] == '0') return false;
    if (std::stoi(part) > 255) return false;
    octets++;
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return octets == 4;
}

std::optional<bool> switch_word(const std::string& w) {
  if (w == "on" || w == "1" || w == "true" || w == "yes") return true;
  if (w == "off" || w == "0" || w == "false" || w == "no" || w.empty()) return false;
  return std::nullopt;
}

Config::Config(std::optional<std::string> file, std::string path, std::string home, const py::PathRules& paths,
               Files* files)
    : path_(std::move(path)), home_(std::move(home)), paths_(paths) {
  read_ok_ = ini_.read(file, path_, &read_error_, files);
}

#define TCR_PROPAGATE(T, r)                     \
  if (!(r).ok()) return Result<T>::fail((r).error)

Result<std::string> Config::get(const std::string& section, const std::string& option,
                                const std::string& fallback) const {
  if (!read_ok_) return Result<std::string>::fail(read_error_);
  return ini_.get(section, option, fallback);
}

Result<std::string> Config::getint(const std::string& section, const std::string& option, int64_t fallback) const {
  if (!read_ok_) return Result<std::string>::fail(read_error_);
  return ini_.getint(section, option, fallback);
}

Result<bool> Config::switch_on(const std::string& section, const std::string& option,
                               const std::string& fallback) const {
  Result<std::string> r = get(section, option, fallback);
  TCR_PROPAGATE(bool, r);
  std::string w = py::lower(py::strip(*r.value));
  return Result<bool>::of(w == "on" || w == "1" || w == "true" || w == "yes");
}

Result<std::string> Config::server_ip_setting() const {
  Result<std::string> r = get("server", "ip", "");
  TCR_PROPAGATE(std::string, r);
  return Result<std::string>::of(py::strip(*r.value));
}

Result<std::optional<std::string>> Config::advertised_ip() const {
  using R = Result<std::optional<std::string>>;
  Result<std::string> r = server_ip_setting();
  TCR_PROPAGATE(std::optional<std::string>, r);
  const std::string& v = *r.value;
  return R::of(!v.empty() && is_ipv4(v) ? std::optional<std::string>(v) : std::nullopt);
}

Result<std::optional<std::string>> Config::server_ip_problem() const {
  using R = Result<std::optional<std::string>>;
  Result<std::string> r = server_ip_setting();
  TCR_PROPAGATE(std::optional<std::string>, r);
  const std::string& v = *r.value;
  if (v.empty() || is_ipv4(v)) return R::of(std::nullopt);
  return R::of("config.ini [server] ip = " + v +
               " is not an IPv4 address (like 203.0.113.5); it is ignored and the server announces the address the "
               "game reaches it at");
}

Result<std::string> Config::server_ip(const std::string& lan_ip) const {
  Result<std::optional<std::string>> r = advertised_ip();
  TCR_PROPAGATE(std::string, r);
  return Result<std::string>::of(*r.value ? **r.value : lan_ip);
}

Result<std::string> Config::http_port() const { return getint("server", "http_port", 80); }

Result<std::string> Config::https_port() const { return getint("server", "https_port", 443); }

Result<std::pair<std::string, std::string>> Config::tls_cert() const {
  using P = std::pair<std::string, std::string>;
  Result<std::string> cert = get("server", "tls_cert", "certs/server.crt");
  TCR_PROPAGATE(P, cert);
  Result<std::string> key = get("server", "tls_key", "certs/server.key");
  TCR_PROPAGATE(P, key);
  return Result<P>::of(P(paths_.join(home_, *cert.value), paths_.join(home_, *key.value)));
}

Result<std::optional<std::string>> Config::ps4_ip() const {
  using R = Result<std::optional<std::string>>;
  Result<std::string> r = get("ps4", "ip", "");
  TCR_PROPAGATE(std::optional<std::string>, r);
  return R::of(r.value->empty() ? std::nullopt : std::optional<std::string>(*r.value));
}

Result<std::pair<std::string, std::string>> Config::relay_address() const {
  using P = std::pair<std::string, std::string>;
  Result<std::string> ip = get("relay", "ip", "0.0.0.0");
  TCR_PROPAGATE(P, ip);
  Result<std::string> port = getint("relay", "port", 4000);
  TCR_PROPAGATE(P, port);
  return Result<P>::of(P(*ip.value, *port.value));
}

Result<std::string> Config::configured_save_path() const {
  Result<std::string> r = get("server", "save_path", "");
  TCR_PROPAGATE(std::string, r);
  std::string path = py::strip(*r.value);
  std::string slashed = path;
  for (char& c : slashed)
    if (c == '\\') c = '/';
  return Result<std::string>::of(slashed.empty() || slashed == "save/player.json" ? std::string() : path);
}

Result<bool> Config::save_path_configured() const {
  Result<std::string> r = configured_save_path();
  TCR_PROPAGATE(bool, r);
  return Result<bool>::of(!r.value->empty());
}

Result<std::string> Config::save_path() const {
  Result<std::string> r = configured_save_path();
  TCR_PROPAGATE(std::string, r);
  std::string path = r.value->empty() ? paths_.join(paths_.join("save", "1.21"), "player.json") : *r.value;
  return Result<std::string>::of(paths_.isabs(path) ? path : paths_.join(home_, path));
}

Result<std::optional<std::string>> Config::game_data() const {
  using R = Result<std::optional<std::string>>;
  Result<std::string> r = get("server", "game_data", "");
  TCR_PROPAGATE(std::optional<std::string>, r);
  std::string path = py::strip(*r.value);
  if (path.empty()) return R::of(std::nullopt);
  return R::of(paths_.isabs(path) ? path : paths_.join(home_, path));
}

Result<std::string> Config::abilities() const { return get("player", "abilities", "none"); }

Result<std::string> Config::crew_credits() const {
  Result<std::string> r = getint("player", "crew_credits", 1000000);
  TCR_PROPAGATE(std::string, r);
  return Result<std::string>::of((*r.value)[0] == '-' ? std::string("0") : *r.value);
}

Result<bool> Config::crew_credits_refill() const { return switch_on("player", "crew_credits_refill", "on"); }

Result<std::string> Config::dlc() const {
  Result<std::string> r = get("player", "dlc", "off");
  TCR_PROPAGATE(std::string, r);
  std::string w = py::lower(py::strip(*r.value));
  bool all = w == "on" || w == "1" || w == "true" || w == "yes" || w == "all";
  return Result<std::string>::of(all ? "all" : "none");
}

Result<std::string> Config::unlocks() const { return get("player", "unlocks", "none"); }

Result<bool> Config::bots() const { return switch_on("world", "bots", "off"); }

Result<bool> Config::afk_kick() const { return switch_on("world", "afk_kick", "off"); }

Result<std::optional<bool>> Config::ui_enabled() const {
  using R = Result<std::optional<bool>>;
  if (!read_ok_) return R::fail(read_error_);
  if (!ini_.has_option("ui", "enabled")) return R::of(std::nullopt);
  Result<std::string> r = get("ui", "enabled", "");
  TCR_PROPAGATE(std::optional<bool>, r);
  std::string w = py::lower(py::strip(*r.value));
  if (w.empty()) return R::of(std::nullopt);
  return R::of(w == "on" || w == "1" || w == "true" || w == "yes");
}

Result<bool> Config::ui_open() const { return switch_on("ui", "open", "on"); }

Result<std::string> Config::ui_port() const { return getint("ui", "port", 8090); }

Result<std::string> Config::ui_password() const {
  Result<std::string> r = get("ui", "password", "");
  TCR_PROPAGATE(std::string, r);
  return Result<std::string>::of(py::strip(*r.value));
}

Result<std::string> Config::multiplayer_setting(bool* set) const {
  if (!read_ok_) return Result<std::string>::fail(read_error_);
  *set = ini_.has_option("world", "multiplayer");
  if (!*set) return Result<std::string>::of(std::string());
  Result<std::string> r = ini_.get("world", "multiplayer");
  TCR_PROPAGATE(std::string, r);
  return Result<std::string>::of(py::strip(*r.value));
}

Result<std::string> Config::multiplayer() const {
  bool set = false;
  Result<std::string> r = multiplayer_setting(&set);
  TCR_PROPAGATE(std::string, r);
  std::string w = py::lower(*r.value);
  if (!set || w == "lan") return Result<std::string>::of("lan");
  std::optional<bool> on = switch_word(w);
  return Result<std::string>::of(on && *on ? "on" : "off");
}

Result<std::optional<std::string>> Config::multiplayer_problem() const {
  using R = Result<std::optional<std::string>>;
  bool set = false;
  Result<std::string> r = multiplayer_setting(&set);
  TCR_PROPAGATE(std::optional<std::string>, r);
  std::string w = py::lower(*r.value);
  if (!set || w == "lan" || switch_word(w)) return R::of(std::nullopt);
  return R::of("config.ini [world] multiplayer = " + *r.value + " is not on, off or lan");
}

Result<bool> Config::public_set() const {
  if (!read_ok_) return Result<bool>::fail(read_error_);
  return Result<bool>::of(ini_.has_option("server", "public"));
}

Result<std::string> Config::link_home_setting() const {
  Result<std::string> r = get("server", "link_home", "on");
  TCR_PROPAGATE(std::string, r);
  return Result<std::string>::of(py::strip(*r.value));
}

Result<bool> Config::link_home() const {
  Result<std::string> r = link_home_setting();
  TCR_PROPAGATE(bool, r);
  std::optional<bool> on = switch_word(py::lower(*r.value));
  return Result<bool>::of(on && *on);
}

Result<std::optional<std::string>> Config::link_home_problem() const {
  using R = Result<std::optional<std::string>>;
  Result<std::string> r = link_home_setting();
  TCR_PROPAGATE(std::optional<std::string>, r);
  if (switch_word(py::lower(*r.value))) return R::of(std::nullopt);
  return R::of("config.ini [server] link_home = " + *r.value +
               " is not on or off (also allowed: yes/no, true/false, 1/0)");
}

Result<std::pair<Bytes, Bytes>> Config::proxy_key() const {
  using P = std::pair<Bytes, Bytes>;
  static const char kK0[] = "TqLkG0EWgq4QWq9ccUCi82XNB3YKrmIK";
  static const char kIV0[] = "euPvz7ih6SgzlAMM";
  Result<std::string> key = get("proxy", "key", "");
  TCR_PROPAGATE(P, key);
  std::string error;
  std::optional<Bytes> k = py::fromhex(*key.value, &error);
  if (!k) return Result<P>::fail(make("ValueError", error));
  Result<std::string> iv = get("proxy", "iv", "");
  TCR_PROPAGATE(P, iv);
  std::optional<Bytes> v = py::fromhex(*iv.value, &error);
  if (!v) return Result<P>::fail(make("ValueError", error));
  if (k->empty()) k = Bytes(kK0, kK0 + 32);
  if (v->empty()) v = Bytes(kIV0, kIV0 + 16);
  return Result<P>::of(P(*k, *v));
}

namespace {

Outcome crash(const Error& e) { return Outcome{Outcome::kCrash, e.type + ": " + e.message}; }

}

Outcome check_config(const Config& c) {
  Error err;
  std::optional<std::string> problem;
  Result<std::optional<std::string>> lh = c.link_home_problem();
  if (lh.ok() && *lh.value) {
    problem = *lh.value;
  } else if (lh.ok()) {
    Result<std::optional<std::string>> mp = c.multiplayer_problem();
    if (!mp.ok()) {
      err = mp.error;
    } else {
      problem = *mp.value;
    }
  } else {
    err = lh.error;
  }
  if (!err.type.empty()) {
    if (!err.configparser()) return crash(err);
    return Outcome{Outcome::kExit, "ERROR: config.ini cannot be read: " + err.message +
                                       "\nOpen config.ini and change the existing lines; each [section] heading and "
                                       "each key may appear only once."};
  }
  if (problem) return Outcome{Outcome::kExit, "ERROR: " + *problem + ". The server does not start."};
  return Outcome{};
}

Outcome say_settings(const Config& c, std::vector<std::string>* lines) {
  Result<bool> pub = c.public_set();
  if (!pub.ok()) return crash(pub.error);
  if (*pub.value) lines->push_back("[server] public has no effect any more: the server always checks names");
  Result<std::string> https = c.https_port();
  if (!https.ok()) return crash(https.error);
  if (*https.value == "0") {
    lines->push_back("WARNING: PS4 consoles send their key only over HTTPS; with https_port = 0 they cannot log in");
  }
  return Outcome{};
}

Outcome check_server_ip(const Config& c, std::vector<std::string>* lines) {
  Result<std::optional<std::string>> p = c.server_ip_problem();
  if (!p.ok()) return crash(p.error);
  if (*p.value) lines->push_back("ERROR: " + **p.value);
  return Outcome{};
}

Outcome dlc_line(const Config& c, std::vector<std::string>* lines) {
  Result<std::string> d = c.dlc();
  if (!d.ok()) return crash(d.error);
  lines->push_back(std::string("DLC: ") + (*d.value == "all" ? "on" : "off") + " (config.ini [player] dlc)");
  return Outcome{};
}

Outcome warn_link_home(const Config& c, TrustedFn trusted, std::vector<std::string>* lines) {
  Result<std::optional<std::string>> ip = c.advertised_ip();
  if (!ip.ok()) return crash(ip.error);
  Result<bool> lh = c.link_home();
  if (!lh.ok()) return crash(lh.error);
  if (*lh.value && *ip.value && !trusted(**ip.value)) {
    lines->push_back(
        "WARNING: link_home: devices from private addresses join a name without a link window; if a proxy, tunnel "
        "or container port forward sits in front of this server, set link_home = off");
  }
  return Outcome{};
}

Outcome warn_multiplayer_public(const Config& c, TrustedFn trusted, std::vector<std::string>* lines) {
  Result<std::optional<std::string>> ip = c.advertised_ip();
  if (!ip.ok()) return crash(ip.error);
  Result<std::string> mp = c.multiplayer();
  if (!mp.ok()) return crash(mp.error);
  if (*mp.value == "on" && *ip.value && !trusted(**ip.value)) {
    lines->push_back(
        "WARNING: multiplayer = on on a server with a public address: every player gets the addresses of the other "
        "players; use multiplayer = lan on a server on the internet");
  }
  return Outcome{};
}

}
}
