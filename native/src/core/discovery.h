#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/accounts.h"
#include "core/env.h"
#include "core/limits.h"
#include "core/util.h"

namespace tcr {
namespace discovery {

constexpr int DISCOVERY_PORT = 4001;
constexpr char PROBE[] = "TCPS4?";
constexpr char ANSWER[] = "TCPS4!";
constexpr size_t PROBE_SIZE = 6;
constexpr uint8_t BOTS = 1, AFK = 2, DLC = 4;
constexpr int REPLIES_PER_S = 1;
constexpr int LINES_PER_MIN = 20;

struct Settings {
  int64_t relay_port = 0;
  int64_t http_port = 0;
  std::optional<std::string> advertise;
  bool bots = false;
  bool afk = false;
  int64_t https_port = 0;
  bool dlc = false;
  Bytes server_id;
};

class Route {
 public:
  virtual ~Route() = default;
  virtual bool local_ip_for(const std::string& peer, std::string* ip, std::string* error) = 0;
};

enum class Answer {
  kNone,
  kReply,
  kRaised,
};

Answer answer(const Bytes& probe, const std::string& peer, const Settings& s, Route& route, Bytes* reply,
              Raised* raised);

class Sender {
 public:
  virtual ~Sender() = default;
  virtual bool send(const Bytes& reply) = 0;
};

class Discovery {
 public:
  Discovery(Env& env, Settings settings, Route& route)
      : env_(env), settings_(std::move(settings)), route_(route), replies_(REPLIES_PER_S, 1.0) {}

  void datagram(const Bytes& data, const std::string& ip, Sender& send);

  const Settings& settings() const { return settings_; }

 private:
  bool may_print(double now);

  Env& env_;
  Settings settings_;
  Route& route_;
  RateLimit replies_;
  double window_ = -1e9;
  int64_t count_ = 0;
  int64_t left_out_ = 0;
};

std::string client_text(const Bytes& data);

}
}
