#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/accounts.h"
#include "core/avatar.h"
#include "core/config.h"
#include "core/json.h"
#include "core/limits.h"
#include "core/ordered_map.h"
#include "core/server_id.h"

namespace tcr {

class Core;
namespace entities {
struct Entry;
struct File;
}

namespace ubi {

constexpr const char kAppId[] = "103649f4-9ca6-49a7-a122-522f3099854b";
constexpr const char kSpaceId[] = "6fbfb85f-567c-49ae-89fe-48fc6fcbd8ce";
constexpr const char kConsoleAuth[] = "tcr-console";
constexpr int kConsoleUploadsPerMin = 2;
constexpr double kOnlineTimeout = 90.0;
constexpr double kTicketTtl = 3 * 3600;
constexpr int kFailLimit = 5;
constexpr double kFailWindow = 600.0;
constexpr int kClaimLimit = 3;
constexpr double kClaimWindow = 3600.0;
constexpr int kClaimAllLimit = 200;
constexpr int kLoginLimit = 30;
constexpr double kLoginWindow = 600.0;
constexpr size_t kNameMax = 32;

std::string stable_id(const std::string& kind, const std::string& name);
std::string uuid4_text(const uint8_t* b);
std::string ubi_time(const LocalTime& t);
std::string ticket_name(const std::string& auth);
std::string credential(const std::string& auth);
bool trusted(const std::string& ip);
std::optional<uint32_t> client_ipv4(const std::string& ip);
bool name_allowed(const std::string& name);
std::pair<std::string, std::string> login_name(const json::Value& body, const std::string& auth);
std::vector<std::pair<std::string, std::string>> parse_qsl(const std::string& qs);
std::optional<std::string> b64decode_strict(const std::string& s);
std::string b64encode(const std::string& data);
json::Value loggable(const json::Value& reply);

struct Reply {
  int status = 200;
  bool binary = false;
  json::Value json;
  std::string bytes;
  bool raised = false;
  Raised exception;
  std::string payload() const { return binary ? bytes : json::dumps(json); }
};

struct Ticket {
  std::string name, platform;
  double issued = 0;
  std::optional<std::string> claim;
  std::string login, ip;
  double wall = 0;
  bool operator==(const Ticket& o) const {
    return name == o.name && platform == o.platform && issued == o.issued && claim == o.claim && login == o.login &&
           ip == o.ip && wall == o.wall;
  }
};

constexpr const char kTicketsName[] = "tickets.json";
std::string ticket_key(const std::string& ticket);

class Ubi {
 public:
  explicit Ubi(Core& core);

  Reply route(const std::string& method, const std::string& path, const json::Value& body, const std::string& host,
              const std::string& client_ip, int client_port, const std::string& auth, const std::string& raw);
  void say(const std::string& client_ip, const std::string& text, bool verified = false);
  bool live_ticket(const std::string& auth, Raised* raised = nullptr);
  std::optional<std::pair<std::string, std::string>> ticket_owner(const std::string& ticket, bool refresh = false,
                                                                  Raised* raised = nullptr);
  void active(const std::string& name);
  void touch(const std::string& name, const std::string& client_ip);
  std::optional<std::string> online_elsewhere(const std::string& name, const std::string& client_ip);
  void logout(const std::string& client_ip);

  AccountStore& accounts() { return *accounts_; }
  ServerId& server_id() { return *server_id_; }
  avatar::Avatars& avatars() { return *avatars_; }
  void set_http_port(int port) { http_port_ = port; }
  const std::string* session_name(const std::string& client_ip) const { return sessions_.get(client_ip); }
  const std::string* session_platform(const std::string& client_ip) const { return platforms_.get(client_ip); }

 private:
  using Times = OrderedMap<std::vector<double>>;
  double now();
  void print(const std::string& text);
  bool expired(const Ticket& t, double now);
  bool issue(const std::string& ticket, const std::string& name, const std::string& platform, const std::string& login,
             const std::string& client_ip, Raised* raised);
  void load_tickets();
  void save_tickets();
  static void prune(Times& table, double window, double now);
  std::optional<double> take(Times& table, int limit, double window, const std::string& ip);
  void give_back(Times& table, double window, const std::string& ip, double stamp);
  Reply refused(const std::string& client_ip, const std::string& name, const std::string& why,
                const std::string& short_why = "");
  std::optional<Reply> check_login(const std::string& name, const std::string& platform, const std::string& auth,
                                   const std::string& client_ip, Raised* raised);
  Reply console_picture(const std::string& name, const std::string& auth, const std::string& raw,
                        const std::string& client_ip);
  json::Value session(const json::Value& body, const std::string& client_ip, const std::string& auth,
                      Raised* raised);
  json::Value configuration(const std::string& host);
  json::Value profiles(const std::string& query);
  void instant_events(const std::string& profile, const json::Value& body, const std::string& auth, Raised* raised);
  json::Value version_reply();
  bool link_home(Raised* raised);
  std::optional<std::string> club_owner(const std::string& profile, const std::string& auth, Raised* raised);
  std::optional<json::Value> club_rewards(Raised* raised);
  json::Value club_actions(const std::string& name);
  json::Value complete_actions(const std::string& name, const json::Value& body);
  std::set<int64_t> completed_actions(const std::string& name);
  std::optional<std::string> entity_owner(const std::string& auth, Raised* raised);
  bool own_profile(const std::string& owner, const json::Value* profile);
  std::optional<json::Value> entity_reply(const std::string& key, const std::string& name, const entities::Entry& e,
                                          const std::string& host, const std::string* space, Raised* raised,
                                          bool owner = false);
  entities::Entry keyed(const std::string& stem, const std::string& owner, entities::File& data,
                        const std::string& name);
  Reply search_entities(bool profiles, const std::string& profile, const std::string& query, const std::string& auth,
                        const std::string& host);
  Reply write_entity(const std::optional<std::string>& eid, const json::Value& body, const std::string& auth,
                     const std::string& host, const std::string* profile = nullptr);
  Reply delete_entity(const std::string& eid, const json::Value& body, const std::string& auth);
  Reply get_entity(const std::string& eid, const std::string& auth, const std::string& host);
  Reply ghost_storage(const std::string& method, const std::string& eid, const std::string& raw,
                      const std::string& query);

  Core& core_;
  std::unique_ptr<AccountStore> accounts_;
  std::unique_ptr<ServerId> server_id_;
  std::unique_ptr<avatar::Avatars> avatars_;
  const py::PathRules& paths_ = py::native_paths();
  std::string tickets_path_;
  int http_port_ = 80;

  OrderedMap<std::string> sessions_;
  OrderedMap<std::string> platforms_;
  OrderedMap<std::pair<std::string, std::string>> profiles_;
  OrderedMap<std::pair<std::string, double>> online_;
  OrderedMap<Ticket> tickets_;
  OrderedMap<double> active_;
  Times failed_, claims_, claims_all_, logins_;
  LineLimit lines_{"http"};
  LineLimit refusals_{"login refusals"};
  RateLimit console_uploads_{kConsoleUploadsPerMin, 60.0};
};

}
}
