#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/env.h"
#include "core/files.h"
#include "core/json.h"
#include "core/ordered_map.h"
#include "core/util.h"

namespace tcr {

constexpr const char kAccountsStoreName[] = "accounts.json";
constexpr const char kLinks[] = "@links";
constexpr double kLinkMinutes = 10;
constexpr size_t kDevicesMax = 16;
constexpr double kVerifiedTtl = 600.0;
constexpr size_t kVerifiedMax = 4096;

std::string save_name(const std::string& name);
std::string account_key(const std::string& name);
constexpr const char kDefaultName[] = "Player";

struct Raised {
  std::string type;
  std::string text;
  std::string line() const { return text.empty() ? type : type + ": " + text; }
};

class AccountStore {
 public:
  AccountStore(Env& env, Files& files, std::string path, std::array<uint8_t, 32> process_key,
               int64_t owner_uid = -1, int64_t owner_gid = -1);

  std::string check(const std::string& name, const std::string& platform, const std::string& secret, bool claim,
                    const std::string& link, Raised* raised = nullptr);
  std::optional<bool> open_link(const std::string& name, double minutes, Raised* raised = nullptr);
  bool free(const std::string& name, Raised* raised = nullptr);
  std::optional<std::string> display_name(const std::string& name, Raised* raised = nullptr);
  std::optional<std::string> account_id(const std::string& name, Raised* raised = nullptr);
  json::Value entries(Raised* raised = nullptr);

  const std::string& path() const { return path_; }

 private:
  using Err = std::optional<Raised>;
  bool read(json::Value* data, std::optional<std::pair<int64_t, uint64_t>>* sig, Err& err);
  const json::Value* load(Err& err);
  bool save(const json::Value& data);
  std::optional<std::string> hash(const std::string& secret, const Bytes& salt, Err& err);
  std::string verify(const std::string& key, const json::Value& entry, const std::string& secret, Err& err);
  std::optional<std::string> mark(const std::string& key, const std::string& salt, const std::string& secret,
                                  Err& err) const;
  void remember(const std::string& mark, double now);
  bool linked_meanwhile(const std::string& key, const json::Value& entry, const json::Value& current,
                        const std::string& secret, Err& err);
  std::optional<std::string> may_link(const std::string& key, const json::Value& entry, const json::Value& data,
                                      const std::string& secret, const std::string& link, Err& err);
  std::string link(const std::string& key, const json::Value& entry, const std::string& platform,
                   const std::string& secret, const std::string& link, Err& err);
  bool window_open(const json::Value& data, const std::string& key);

  Env& env_;
  Files& files_;
  std::string path_;
  std::array<uint8_t, 32> process_key_;
  int64_t owner_uid_, owner_gid_;
  bool have_data_ = false;
  json::Value data_;
  std::optional<std::pair<int64_t, uint64_t>> sig_;
  OrderedMap<double> verified_;
};

std::optional<Raised> utf8_encode_error(const std::string& text);

}
