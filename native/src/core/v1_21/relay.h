#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/accounts.h"
#include "core/config.h"
#include "core/json.h"
#include "core/limits.h"
#include "core/ordered_map.h"
#include "core/session_store.h"
#include "core/util.h"
#include "core/v1_21/handlers.h"
#include "core/wire.h"

namespace tcr {
class Core;
namespace v1_21 {

constexpr uint32_t GET_PLAYER = 0x589F57FB;
constexpr double GUEST_IDLE_S = 120;
constexpr double NAMED_IDLE_S = 15 * 60;
constexpr size_t MAX_GUESTS = 2000;
constexpr size_t MAX_GUESTS_PER_IP = 32;
constexpr size_t REPLY_CACHE = 256;
constexpr int64_t REPLY_BYTES = 4ll * 1024 * 1024;
constexpr size_t GUEST_REPLY_CACHE = 32;
constexpr int64_t GUEST_REPLY_BYTES = 64 * 1024;
constexpr int64_t REPLY_BYTES_TOTAL = 256ll * 1024 * 1024;
constexpr double REQUESTS_PER_S = 100;
constexpr double REQUEST_BURST = 500;
constexpr double REPLY_BYTES_PER_S = 1024 * 1024;
constexpr double REPLY_BYTES_BURST = 16.0 * 1024 * 1024;
constexpr size_t BUDGET_ADDRESSES = 10000;
constexpr double BUDGET_IDLE_S = 600;
constexpr int64_t HANDSHAKES_PER_S = 10;
constexpr double PLAYER_IDLE_S = 30 * 60;
constexpr size_t MAX_UNKNOWN = 1000;
constexpr double SAY_WINDOW_S = 60;
constexpr int64_t SAY_UNVERIFIED_PER_MIN = 20;
constexpr int64_t LOG_UNVERIFIED_PER_MIN = 2000;

struct Addr {
  Addr() = default;
  Addr(std::string ip_, int port_) : ip(std::move(ip_)), port(port_) {}
  std::string ip;
  int port = 0;
  std::string port_text;
  std::string key() const;
  std::string text() const;
};

struct Packet {
  Bytes data;
  Addr dst;
};

class RelayHost {
 public:
  virtual ~RelayHost() = default;
  virtual bool append_log(const std::string& path, const std::string& line, std::string* why);
  virtual void log_failed(const std::string& path, const std::string& why);
  virtual bool local_ip_for(const std::string& peer, std::string* ip, Raised* raised);
};

struct RelayOptions {
  std::string ip;
  int port = 0;
  std::string log_path;
  std::string save_path;
  std::string abilities = "none";
  std::string unlocks = "none";
  std::string dlc = "all";
  Bytes proxy_key;
  Bytes proxy_iv;
};

struct Session {
  Bytes key, iv;
  bool connected = false;
  OrderedMap<std::optional<bool>> answered;
  OrderedMap<std::vector<Bytes>> replies;
  int64_t reply_bytes = 0;
  std::shared_ptr<wire::Partial> partial = std::make_shared<wire::Partial>();
  std::shared_ptr<Ctx> ctx;
  std::optional<std::string> name;
  std::optional<json::Value> odd_name;
  json::Value platform;
  bool restored = false;
  bool refused = false;
  double stamp = 0.0;
  double seen = 0.0;
};

struct DatagramResult {
  std::vector<Packet> packets;
  std::optional<Raised> raised;
};

class Relay {
 public:
  Relay(Core& core, RelayOptions options, RelayHost* host = nullptr);
  virtual ~Relay();

  const std::optional<Raised>& start_error() const { return start_error_; }

  DatagramResult handle_datagram(const Bytes& data, const Addr& addr);
  void datagram_done() {
    muted_ = false;
    current_.reset();
  }

  void send_failed(const Addr& to, const std::string& error);
  void handler_error(const Bytes& data, const Addr& from, const std::string& traceback);

  const Session* session(const Addr& addr) const;
  int port() const { return o_.port; }
  std::vector<std::pair<Addr, const Session*>> sessions() const;
  const std::string& save_path() const { return o_.save_path; }

 protected:
  virtual std::string rpc_name(uint32_t id) const;
  virtual HandleResult dispatch(Ctx& ctx, uint32_t id, const Bytes& body);
  virtual std::optional<Bytes> seal(const Session& sess, const Bytes& plain, Raised* raised);
  virtual bool guest_request(uint32_t rpc) const;
  uint32_t get_player_ = GET_PLAYER;
  size_t chunk_ = wire::CHUNK;

  struct Budget {
    double tokens, reply_bytes, at;
  };
  struct Entry {
    Addr addr;
    std::shared_ptr<Session> sess;
  };

  double clock();
  std::string ts();
  void print(const std::string& line);
  void restore();
  void remember(Session* sess, const Addr& addr);
  Session& new_session(const Addr& addr);
  void guest_active(const Addr& addr);
  void unguest(const Addr& addr);
  void expire(double now);
  void drop(const Addr& addr);
  void forget_session(const Addr& addr);
  void cache_reply(Session& sess, const Addr& addr, const std::string& key, const std::vector<Bytes>& chunks);
  void store_named();
  bool new_ctx(const std::string& name, const std::string& path, std::shared_ptr<Ctx>* out, Raised* raised);
  bool ctx_for_name(const std::string& name, std::shared_ptr<Ctx>* out, Raised* raised);
  bool ctx_for(const Addr& addr, Session* sess, std::shared_ptr<Ctx>* out, Raised* raised);
  bool public_ip(const Addr& peer, std::string* out, Raised* raised);
  void log(json::Value entry);
  bool may_log();
  void guest_dropped(const Addr& addr, const std::string& text);
  void evicted_dropped(const Addr& addr, const std::string& text);
  virtual void say(const Addr& addr, const std::string& text, bool always = false);
  bool may_say(const Addr& addr);
  Budget& budget(const std::string& ip);
  bool may_request(const Addr& addr);
  bool bytes_ok(const Addr& addr);
  void charge_bytes(const Addr& addr, int64_t n);
  std::optional<std::vector<Bytes>> ack_for(const wire::Message& msg, size_t received, const Ctx& ctx, Raised* raised);
  bool reply_chunks(const wire::Message& msg, uint32_t rpc, const Bytes& request, Ctx& ctx,
                    std::optional<std::vector<Bytes>>* chunks, DatagramResult* result);
  virtual void probe(const Addr& addr, DatagramResult* r);
  virtual void key_exchange(const Addr& addr, DatagramResult* r);
  void direct(const Bytes& header, const Bytes& body, const Addr& addr, DatagramResult* r);
  bool bind_player(Session& sess, const Addr& addr, const Bytes& request, bool* bound, DatagramResult* r);
  virtual void tunnel(Session& sess, const Bytes& header, const Bytes& body, const Addr& addr, DatagramResult* r);
  void tunnel_plain(Session& sess, const Bytes& header, const Bytes& plain, const Addr& addr, DatagramResult* r);
  Session* find(const Addr& addr) const;

  Core& core_;
  RelayOptions o_;
  RelayHost default_host_;
  RelayHost* host_;
  std::optional<Raised> start_error_;
  OrderedMap<Entry> sessions_;
  std::unique_ptr<session::SessionStore> store_;
  std::unordered_set<std::string> unknown_;
  OrderedMap<Addr> guests_;
  OrderedMap<OrderedMap<Addr>> guests_by_ip_;
  OrderedMap<Addr> unproven_;
  RateLimit handshakes_{HANDSHAKES_PER_S, 1.0};
  OrderedMap<Budget> budgets_;
  OrderedMap<std::shared_ptr<Session>> proven_;
  OrderedMap<Addr> guest_caches_;
  OrderedMap<Addr> player_caches_;
  int64_t reply_total_ = 0;
  std::unordered_set<std::string> stored_;
  double swept_ = 0.0;
  OrderedMap<std::shared_ptr<wire::Partial>> direct_partial_;
  bool muted_ = false;
  OrderedMap<double> said_;
  double say_window_ = 0.0;
  int64_t say_count_ = 0, say_left_out_ = 0;
  LineLimit guest_lines_;
  LineLimit evicted_lines_;
  std::optional<Addr> current_;
  double log_window_ = 0.0;
  int64_t log_count_ = 0, log_left_out_ = 0;
  OrderedMap<std::shared_ptr<Ctx>> players_;
  OrderedMap<double> player_seen_;
  std::shared_ptr<Ctx> ctx_;
};

bool start_crew_credits(Core& core, Player& p, bool fresh, Raised* raised);

Bytes hide_tickets(const Bytes& data);
std::optional<std::string> player_name(const Bytes& body);
std::optional<std::string> player_ticket(const Bytes& body);

}
}
