#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/files.h"
#include "core/util.h"

namespace tcr {
namespace config {

struct Error {
  std::string type;
  std::string message;
  bool configparser() const;
};

template <class T>
struct Result {
  std::optional<T> value;
  Error error;
  bool ok() const { return value.has_value(); }
  static Result fail(Error e) {
    Result r;
    r.error = std::move(e);
    return r;
  }
  static Result of(T v) {
    Result r;
    r.value = std::move(v);
    return r;
  }
};

class Ini {
 public:
  bool read(const std::optional<std::string>& file, const std::string& path, Error* err, Files* files = nullptr);

  bool has_section(const std::string& section) const;
  bool has_option(const std::string& section, const std::string& option) const;
  Result<std::string> get(const std::string& section, const std::string& option, const std::string& fallback) const;
  Result<std::string> get(const std::string& section, const std::string& option) const;
  Result<std::string> getint(const std::string& section, const std::string& option, int64_t fallback) const;

 private:
  struct Option {
    std::string key;
    std::vector<std::string> lines;
    std::string value;
  };
  struct Section {
    std::string name;
    std::vector<Option> options;
    Option* find(const std::string& key);
    const Option* find(const std::string& key) const;
  };
  Section* section(const std::string& name);
  const Section* section(const std::string& name) const;
  const std::string* lookup(const Section* sect, const std::string& option) const;
  bool interpolate(const std::string& section, const std::string& option, const std::string& rawval,
                   const Section* sect, std::string& accum, const std::string& rest, int depth, Error* err) const;

  std::vector<Section> sections_;
  Section defaults_{"DEFAULT", {}};
};

class Config {
 public:
  Config(std::optional<std::string> file, std::string path, std::string home, const py::PathRules& paths,
         Files* files = nullptr);

  const Error* read_error() const { return read_ok_ ? nullptr : &read_error_; }
  const Ini& ini() const { return ini_; }

  Result<std::string> server_ip_setting() const;
  Result<std::optional<std::string>> advertised_ip() const;
  Result<std::optional<std::string>> server_ip_problem() const;
  Result<std::string> server_ip(const std::string& lan_ip) const;
  Result<std::string> http_port() const;
  Result<std::string> https_port() const;
  Result<std::pair<std::string, std::string>> tls_cert() const;
  Result<std::optional<std::string>> ps4_ip() const;
  Result<std::pair<std::string, std::string>> relay_address() const;
  Result<bool> save_path_configured() const;
  Result<std::string> save_path() const;
  Result<std::optional<std::string>> game_data() const;
  Result<std::string> abilities() const;
  Result<std::string> crew_credits() const;
  Result<bool> crew_credits_refill() const;
  Result<std::string> dlc() const;
  Result<std::string> unlocks() const;
  Result<bool> bots() const;
  Result<std::string> multiplayer() const;
  Result<std::optional<std::string>> multiplayer_problem() const;
  Result<bool> afk_kick() const;
  Result<bool> public_set() const;
  Result<bool> link_home() const;
  Result<std::optional<std::string>> link_home_problem() const;
  Result<std::pair<Bytes, Bytes>> proxy_key() const;
  Result<std::optional<bool>> ui_enabled() const;
  Result<bool> ui_open() const;
  Result<std::string> ui_port() const;
  Result<std::string> ui_password() const;

 private:
  Result<std::string> get(const std::string& section, const std::string& option, const std::string& fallback) const;
  Result<std::string> getint(const std::string& section, const std::string& option, int64_t fallback) const;
  Result<bool> switch_on(const std::string& section, const std::string& option, const std::string& fallback) const;
  Result<std::string> configured_save_path() const;
  Result<std::string> multiplayer_setting(bool* set) const;
  Result<std::string> link_home_setting() const;

  Ini ini_;
  bool read_ok_ = false;
  Error read_error_;
  std::string path_;
  std::string home_;
  const py::PathRules& paths_;
};

bool is_ipv4(const std::string& value);

std::optional<bool> switch_word(const std::string& lowered);

struct Outcome {
  enum Kind { kOk, kExit, kCrash } kind = kOk;
  std::string text;
};

Outcome check_config(const Config& c);
Outcome say_settings(const Config& c, std::vector<std::string>* lines);
Outcome check_server_ip(const Config& c, std::vector<std::string>* lines);
Outcome dlc_line(const Config& c, std::vector<std::string>* lines);
using TrustedFn = bool (*)(const std::string& ip);
Outcome warn_link_home(const Config& c, TrustedFn trusted, std::vector<std::string>* lines);
Outcome warn_multiplayer_public(const Config& c, TrustedFn trusted, std::vector<std::string>* lines);

}
}
