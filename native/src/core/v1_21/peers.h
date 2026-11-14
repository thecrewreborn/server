#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/accounts.h"
#include "core/util.h"

namespace tcr {
class Core;
namespace v1_21 {

struct Ctx;

namespace peers {

constexpr size_t MAX_PEERS = 5;
constexpr double STALE_S = 120;
constexpr uint32_t NET_ENTITY = 0xC8337621;
constexpr uint32_t P_PEER = 0x62230DFA;
constexpr uint32_t P_ORBIS = 0x9A42C791;
constexpr uint32_t P_PUNCH = 0x0547E4F3;
constexpr uint32_t SESSION_NUMBER = 1;

enum class Platform { kNone, kPs4, kPc };

struct Peer {
  uint64_t player_id = 0;
  Bytes info;
  Bytes entity;
  double x = 0.0;
  double y = 0.0;
  double seen = 0.0;
  int64_t order = 0;

  uint32_t key() const;
  Platform platform() const;
  std::string name() const;
  Bytes element(const Peer* viewer) const;
};

class Peers {
 public:
  explicit Peers(Core& core) : core_(core) {}

  void add(uint64_t pid, Bytes info, Bytes entity, double x, double y);
  void forget(uint64_t pid);
  void moved(uint64_t pid, double x, double y);
  std::vector<std::pair<double, size_t>> others(uint64_t pid, double x, double y) const;
  std::vector<size_t> hosts(uint64_t pid) const;

  const Peer* get(uint64_t pid) const;
  const std::vector<Peer>& all() const { return by_id_; }
  int64_t count() const { return count_; }
  void clear() {
    by_id_.clear();
    count_ = 0;
  }

 private:
  double clock() const;
  Peer* find(uint64_t pid);

  Core& core_;
  std::vector<Peer> by_id_;
  int64_t count_ = 0;
};

std::vector<std::pair<uint32_t, Bytes>> entity_properties(const Bytes& entity);
Bytes net_entity(const std::vector<std::pair<uint32_t, Bytes>>& props);
std::pair<uint32_t, Bytes> platform_property(Platform platform, const std::string& name);

bool multiplayer(Core& core, std::string* mode, Raised* raised);
bool enabled(Core& core, bool* on, Raised* raised);
bool acting_id(Core& core, const Ctx& ctx, std::optional<uint64_t>* out, Raised* raised);

bool info_end(const Bytes& body, size_t p, size_t* end, Raised* raised);

struct Registration {
  uint64_t pid = 0;
  Bytes info;
  Bytes entity;
  double x = 0.0;
  double y = 0.0;
};
bool parse_register(const Bytes& body, Registration* out, Raised* raised);
bool register_peer(Core& core, const Bytes& body, const Ctx* ctx, std::optional<uint64_t>* pid, Raised* raised);

bool session_reply(Core& core, std::optional<uint64_t> pid, double x, double y, Bytes* out, Raised* raised);
bool nearby_reply(Core& core, std::optional<uint64_t> pid, double x, double y, Bytes* out, Raised* raised);
bool session_join_reply(Core& core, const Bytes& body, const Ctx* ctx, Bytes* out, Raised* raised);

}
}
}
