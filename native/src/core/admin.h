#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"

namespace tcr {
class Core;
namespace v1_21 {
class Relay;
}

namespace admin {

struct RelayView {
  std::string version;
  const v1_21::Relay* relay = nullptr;
  int port = 0;
};

struct Facts {
  std::string version;
  std::string address;
  std::string lan_ip;
  int http_port = 0, https_port = 0, discovery_port = 0, admin_port = 0;
  std::string relay_ip;
  std::string server_id;
  double started_wall = 0.0;
  double started_mono = 0.0;
  double uptime = 0.0;
};

json::Value facts(Core& core, const Facts& f, const std::vector<RelayView>& relays);

json::Value clients(Core& core, const std::vector<RelayView>& relays);

json::Value names(Core& core);

std::string save_folder(Core& core, const RelayView& relay);

bool inside_home(Core& core, const std::string& path);

bool save_file_name_ok(const std::string& file);

json::Value saves(Core& core, const std::vector<RelayView>& relays);

std::string save_file(Core& core, const std::vector<RelayView>& relays, const std::string& version,
                      const std::string& file);

std::string save_view(const std::string& data, bool* formatted);

std::string valid_utf8(const std::string& s, size_t limit = static_cast<size_t>(-1));

std::string unescape_non_ascii(const std::string& dumped);

}
}
