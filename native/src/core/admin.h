#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/util.h"

namespace tcr {
class Core;
class Files;
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

json::Value players(Core& core, const std::vector<RelayView>& relays);

struct DetailContext {
  std::optional<Bytes> pack_garage;
  std::map<uint64_t, int64_t> best;
};

json::Value player_detail(const json::Value& save, const DetailContext& ctx);

int64_t collectibles_found(const json::Value* list);

std::map<uint64_t, int64_t> best_scores(Files* files, const std::string& data, uint64_t player);

json::Value saved_player(const std::string& data, const DetailContext* ctx = nullptr);

struct SavedSource {
  DetailContext ctx;
  std::string scores_path;
  uint64_t player = 0;
};
bool saved_source(Core& core, const std::vector<RelayView>& relays, const std::string& version,
                  const std::string& file, SavedSource* out);

struct LivePlayer {
  json::Value entry;
  std::string save_text;
  DetailContext ctx;
  std::string scores_path;
  uint64_t player = 0;
};
bool live_player(Core& core, const std::vector<RelayView>& relays, const std::string& version,
                 const std::string& name, LivePlayer* out);

json::Value settings(Core& core);

json::Value set_setting(Core& core, const json::Value& body, int* status, std::string* said);

json::Value set_report_choice(Core& core, const json::Value& body, int* status, std::string* said);

std::string valid_utf8(const std::string& s, size_t limit = static_cast<size_t>(-1));

std::string unescape_non_ascii(const std::string& dumped);

}
}
