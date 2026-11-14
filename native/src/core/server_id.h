#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/env.h"
#include "core/files.h"
#include "core/util.h"

namespace tcr {

constexpr size_t kServerIdSize = 16;

class ServerId {
 public:
  ServerId(Env& env, Files& files, std::string path, const py::PathRules& paths = py::native_paths())
      : env_(env), files_(files), path_(std::move(path)), paths_(paths) {}

  std::optional<Bytes> get(bool* damaged = nullptr, std::string* error = nullptr);
  std::optional<std::string> hex_id(bool* damaged = nullptr, std::string* error = nullptr);
  const std::string& path() const { return path_; }

 private:
  bool bad_files(std::vector<std::string>* out, OsError* err);
  Env& env_;
  Files& files_;
  std::string path_;
  const py::PathRules& paths_;
  std::optional<Bytes> cache_;
};

std::optional<std::string> console_code(const Bytes& key, const Bytes& server, const std::string& address,
                                        int port = 443);

}
