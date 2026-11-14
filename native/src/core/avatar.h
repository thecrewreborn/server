#pragma once

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

#include "core/accounts.h"
#include "core/env.h"
#include "core/files.h"
#include "core/limits.h"
#include "core/util.h"

namespace tcr {
namespace avatar {

constexpr int kFit = 256;
constexpr size_t kMaxUpload = 300000;
constexpr uint32_t kMaxSide = 1024;
constexpr size_t kRgbaCacheBytes = 32u << 20;
constexpr int kDecodesPerS = 10;
constexpr const char kConsoleFolder[] = "ps4";

bool match_name(const std::string& path, const char* prefix, const char* suffix, std::string* name);

const std::string& logo();

bool decode(const std::string& data, uint32_t* width, uint32_t* height, std::string* rgba, Raised* raised);
bool encode(uint32_t width, uint32_t height, const std::string& rgba, std::string* png, Raised* raised);
std::string box(uint32_t width, uint32_t height, const std::string& rgba, uint32_t size);
bool fit(const std::string& data, std::string* out, Raised* raised);
std::optional<std::string> check_console_picture(const std::string& data, bool decode = true);

class Avatars {
 public:
  Avatars(Env& env, Files& files, std::string folder, const py::PathRules& paths)
      : env_(env), files_(files), folder_(std::move(folder)), paths_(paths) {}

  std::string for_request(const std::optional<std::string>& name, const std::string& path, Raised* raised);
  std::optional<std::string> rgba_for(const std::optional<std::string>& name, uint32_t size,
                                      const std::string& client_ip, Raised* raised);
  std::optional<bool> save_upload(const std::string& name, const std::string& rgba, Raised* raised);
  std::optional<std::string> save_console_upload(const std::string& name, const std::string& data, Raised* raised);

  const std::string& folder() const { return folder_; }

 private:
  bool valid_name(const std::optional<std::string>& name) const;
  std::string console_path(const std::string& name) const;
  bool fitted(const std::string& path, double mtime, std::string* out, Raised* raised);
  std::optional<std::string> rgba(const std::string& png, uint32_t size, const std::string& client_ip,
                                  Raised* raised);

  Env& env_;
  Files& files_;
  std::string folder_;
  const py::PathRules& paths_;
  std::list<std::pair<std::string, std::string>> rgba_entries_;
  std::unordered_map<std::string, std::list<std::pair<std::string, std::string>>::iterator> rgba_index_;
  size_t rgba_size_ = 0;
  RateLimit decodes_{kDecodesPerS, 1.0};
  std::list<std::pair<std::pair<std::string, double>, std::string>> fitted_;
};

}
}
