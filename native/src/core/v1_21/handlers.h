#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/accounts.h"
#include "core/env.h"
#include "core/util.h"
#include "core/v1_21/player.h"

namespace tcr {
class Core;
namespace v1_21 {

uint64_t player_id_for(const std::string& user_name);

std::optional<int64_t> reward_max_bucks(const Core& core);

struct Ctx {
  std::string player_name = kDefaultName;
  uint64_t player_id = player_id_for(kDefaultName);
  Player player;
  Core* core = nullptr;
  std::optional<int64_t> fixed_now_us;
  bool pc = false;
  uint64_t crew_group = 0;
  bool verified = false;
  std::string client_ip;

  int64_t now_us() const;
  std::optional<int64_t> client_now_us(Raised* raised) const;
};

struct HandlerResult {
  enum Kind { kReply, kNone, kRaised } kind = kReply;
  uint32_t type = 0;
  Bytes body;
  Raised raised;

  static HandlerResult reply(uint32_t type, Bytes body) {
    HandlerResult r;
    r.type = type;
    r.body = std::move(body);
    return r;
  }
  static HandlerResult none() {
    HandlerResult r;
    r.kind = kNone;
    return r;
  }
  static HandlerResult fail(Raised raised) {
    HandlerResult r;
    r.kind = kRaised;
    r.raised = std::move(raised);
    return r;
  }
};

struct Registered;
using Handler = HandlerResult (*)(Ctx& ctx, const Bytes& body, const Registered& self);

struct Registered {
  uint32_t id = 0;
  std::string name;
  Handler fn = nullptr;
  const void* data = nullptr;
};

const std::vector<Registered>& registry();
const Registered* registered(uint32_t id);

std::string name_of(uint32_t id);

struct HandleResult {
  enum Kind {
    kFrame,
    kNoReply,
    kUnknown,
    kRaised,
  } kind = kUnknown;
  Bytes frame;
  Raised raised;
};

HandleResult handle(Ctx& ctx, uint32_t id, const Bytes& body);

bool caught_by_reply_chunks(const Raised& r);

void register_crew(std::vector<Registered>& out);
void register_entity(std::vector<Registered>& out);
void register_garage(std::vector<Registered>& out);
void register_group(std::vector<Registered>& out);
void register_login(std::vector<Registered>& out);
void register_mission(std::vector<Registered>& out);
void register_notify(std::vector<Registered>& out);
void register_police(std::vector<Registered>& out);
void register_presence(std::vector<Registered>& out);
void register_progress(std::vector<Registered>& out);
void register_session(std::vector<Registered>& out);
void register_skill(std::vector<Registered>& out);
void register_stats(std::vector<Registered>& out);
void register_summit(std::vector<Registered>& out);
void register_ticket(std::vector<Registered>& out);
void register_replies(std::vector<Registered>& out);

}
}
