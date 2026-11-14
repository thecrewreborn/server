#include "core/core.h"

#include <utility>

#include "core/ubi.h"
#include "core/v1_21/module_state.h"
#include "core/v1_21/peers.h"

namespace tcr {

Random random_from_urandom(Env& env) {
  uint32_t words[Random::kStateWords];
  uint8_t bytes[sizeof(words)];
  env.urandom(bytes, sizeof(bytes));
  for (int i = 0; i < Random::kStateWords; i++) {
    words[i] = static_cast<uint32_t>(bytes[4 * i]) | static_cast<uint32_t>(bytes[4 * i + 1]) << 8 |
               static_cast<uint32_t>(bytes[4 * i + 2]) << 16 | static_cast<uint32_t>(bytes[4 * i + 3]) << 24;
  }
  return Random::from_words(words, Random::kStateWords);
}

Core::Core(Env& env, std::string home, std::string config_text)
    : env_(&env),
      home_(std::move(home)),
      config_text_(std::move(config_text)),
      module_random_(random_from_urandom(env)) {
  env.urandom(accounts_key_.data(), accounts_key_.size());
}

Core::~Core() = default;

const config::Config& Core::config() {
  const py::PathRules& paths = py::native_paths();
  std::string path = env_->config_path().value_or(paths.join(home_, "config.ini"));
  if (!env_->provides_config()) {
    if (!config_) {
      std::optional<std::string> file;
      if (!config_text_.empty()) file = config_text_;
      config_.reset(new config::Config(file, path, home_, paths, files_));
    }
    return *config_;
  }
  std::optional<std::string> stamp = env_->config_stamp();
  if (!config_ || stamp != config_stamp_) {
    config_.reset(new config::Config(env_->config_read(), path, home_, paths, files_));
    config_stamp_ = stamp;
  }
  return *config_;
}

v1_21::ModuleState& Core::v1_21_state() {
  if (!v1_21_state_) v1_21_state_.reset(new v1_21::ModuleState());
  return *v1_21_state_;
}

ubi::Ubi& Core::ubi() {
  if (!ubi_) ubi_.reset(new ubi::Ubi(*this));
  return *ubi_;
}

v1_21::peers::Peers& Core::peers() {
  if (!peers_) peers_.reset(new v1_21::peers::Peers(*this));
  return *peers_;
}

}
