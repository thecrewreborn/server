#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/discovery.h"
#include "core/env.h"
#include "core/v1_21/relay.h"
#include "platform/files.h"

namespace tcr::app {

std::string console_text(const std::string& text);
void say(const std::string& text);
void say_error(const std::string& text);

std::string home_folder();
std::string config_file(const std::string& home);
std::string first_config_file(const std::string& home);

class SystemEnv final : public Env {
 public:
  SystemEnv(platform::SystemFiles& files, std::string config_path)
      : files_(files), config_path_(std::move(config_path)) {}

  double time() override;
  double monotonic() override;
  void urandom(uint8_t* out, size_t n) override;
  Random unseeded_random() override;
  LocalTime local_time(double t) override;
  LocalTime now_local() override;
  bool now_utc(int64_t* sec, int* us) override;
  void print_line(const std::string& line) override { say(line); }
  bool provides_config() const override { return true; }
  std::optional<std::string> config_stamp() override;
  std::optional<std::string> config_read() override;
  std::optional<std::string> config_path() override { return config_path_; }
  void hash_unlocked(void (*fn)(void*), void* arg) override;

#if defined(TCR_TEST_HOOKS)
  void set_clock(double now);
  void set_deterministic(uint64_t counter);
#endif
  bool deterministic() const { return deterministic_; }

 private:
  platform::SystemFiles& files_;
  std::string config_path_;
  bool fixed_clock_ = false;
  double clock_ = 0.0;
  bool deterministic_ = false;
  uint64_t counter_ = 0;
};

class SystemRoute final : public discovery::Route {
 public:
  bool local_ip_for(const std::string& peer, std::string* ip, std::string* error) override;
};

std::string lan_ip();

class SystemRelayHost final : public v1_21::RelayHost {
 public:
  bool append_log(const std::string& path, const std::string& line, std::string* why) override;
  void log_failed(const std::string& path, const std::string& why) override;
  bool local_ip_for(const std::string& peer, std::string* ip, Raised* raised) override;
};

}
