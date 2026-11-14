#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "core/config.h"
#include "core/env.h"
#include "core/files.h"
#include "core/random.h"
#include "core/wire.h"

namespace tcr {

struct Overrides {
  std::optional<std::string> multiplayer;
  std::optional<bool> link_home;
  bool reward_tables = true;
  bool car_catalog = true;
};

namespace ubi {
class Ubi;
}
namespace v1_21 {
struct ModuleState;
namespace peers {
class Peers;
}
}

class Core {
 public:
  Core(Env& env, std::string home, std::string config_text);
  ~Core();

  Env& env() { return *env_; }
  void set_env(Env& env) { env_ = &env; }
  const std::string& home() const { return home_; }
  const std::string& config_text() const { return config_text_; }
  const config::Config& config();

  Random& module_random() { return module_random_; }
  void set_module_random(const Random& r) { module_random_ = r; }

  Overrides& overrides() { return overrides_; }
  const Overrides& overrides() const { return overrides_; }

  Files* files() { return files_; }
  void set_files(Files* files) { files_ = files; }

  wire::PendingRegistry& pending() { return pending_; }

  ubi::Ubi& ubi();
  const std::array<uint8_t, 32>& accounts_key() const { return accounts_key_; }
  v1_21::peers::Peers& peers();

  v1_21::ModuleState& v1_21_state();

 private:
  Env* env_;
  std::string home_;
  std::string config_text_;
  Random module_random_;
  Overrides overrides_;
  Files* files_ = nullptr;
  wire::PendingRegistry pending_;
  std::array<uint8_t, 32> accounts_key_{};
  std::unique_ptr<ubi::Ubi> ubi_;
  std::unique_ptr<v1_21::ModuleState> v1_21_state_;
  std::unique_ptr<v1_21::peers::Peers> peers_;
  std::unique_ptr<config::Config> config_;
  std::optional<std::string> config_stamp_;
};

}
