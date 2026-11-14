#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/files.h"
#include "core/json.h"
#include "core/util.h"

namespace tcr {
namespace session {

constexpr const char STORE_NAME[] = "sessions.json";
constexpr double STORE_MAX_AGE = 2 * 24 * 3600;

struct Stored {
  std::string ip;
  std::string port;
  Bytes key;
  Bytes iv;
  json::Value name;
  json::Value platform;
  double stamp = 0.0;
};

struct Loaded {
  std::vector<Stored> sessions;
  std::optional<std::string> error;
};

Loaded load(const std::optional<std::string>& file, double now, Files* files = nullptr);
std::string store_text(const std::vector<Stored>& sessions);

class SessionStore {
 public:
  SessionStore(Files& files, std::string path) : files_(files), path_(std::move(path)) {}
  const std::string& path() const { return path_; }
  Loaded load(double now) const;
  std::optional<std::string> save(const std::vector<Stored>& connected) const;

 private:
  Files& files_;
  std::string path_;
};

}
}
