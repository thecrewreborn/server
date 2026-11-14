#include "core/v1_21/handlers.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "core/codec.h"
#include "core/core.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/rewards.h"
#include "core/v1_21/summit.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

uint64_t player_id_for(const std::string& user_name) {
  return 0x9000000000000000ull | wire::crc32(reinterpret_cast<const uint8_t*>(user_name.data()), user_name.size());
}

std::optional<int64_t> reward_max_bucks(const Core& core) {
  const rewards::Tables* t = rewards::tables(core);
  if (!t) return std::nullopt;
  return t->max_bucks();
}

int64_t Ctx::now_us() const {
  if (fixed_now_us) return *fixed_now_us;
  double t = core ? core->env().time() : 0.0;
  return static_cast<int64_t>(t * 1000000.0);
}

std::optional<int64_t> Ctx::client_now_us(Raised* raised) const {
  return summit::calendar_time_us(now_us(), gamedata::summit(), raised);
}

namespace {

std::vector<Registered> build() {
  std::vector<Registered> order;
  register_crew(order);
  register_entity(order);
  register_garage(order);
  register_group(order);
  register_login(order);
  register_mission(order);
  register_notify(order);
  register_police(order);
  register_presence(order);
  register_progress(order);
  register_session(order);
  register_skill(order);
  register_stats(order);
  register_summit(order);
  register_ticket(order);
  register_replies(order);
  std::map<uint32_t, Registered> by_id;
  for (Registered& r : order) by_id[r.id] = std::move(r);
  std::vector<Registered> out;
  for (auto& e : by_id) out.push_back(std::move(e.second));
  return out;
}

}

const std::vector<Registered>& registry() {
  static const std::vector<Registered> r = build();
  return r;
}

const Registered* registered(uint32_t id) {
  const auto& r = registry();
  auto it = std::lower_bound(r.begin(), r.end(), id, [](const Registered& a, uint32_t b) { return a.id < b; });
  return it != r.end() && it->id == id ? &*it : nullptr;
}

std::string name_of(uint32_t id) {
  if (const Registered* r = registered(id)) return r->name;
  char buf[32];
  if (gamedata::auto_reply(id)) {
    std::snprintf(buf, sizeof(buf), "AUTO_0x%08X", id);
  } else {
    std::snprintf(buf, sizeof(buf), "0x%08x", id);
  }
  return buf;
}

HandleResult handle(Ctx& ctx, uint32_t id, const Bytes& body) {
  HandleResult out;
  if (const Registered* r = registered(id)) {
    if (!ctx.core) {
      out.kind = HandleResult::kRaised;
      out.raised = Raised{"RuntimeError", "a request handler without a server core"};
      return out;
    }
    if (std::optional<Raised> odd = ctx.player.clamp()) {
      out.kind = HandleResult::kRaised;
      out.raised = *odd;
      return out;
    }
    HandlerResult res = r->fn(ctx, body, *r);
    if (res.kind == HandlerResult::kRaised) {
      out.kind = HandleResult::kRaised;
      out.raised = std::move(res.raised);
    } else if (res.kind == HandlerResult::kNone) {
      out.kind = HandleResult::kNoReply;
    } else {
      out.kind = HandleResult::kFrame;
      out.frame = wire::frame(res.type, res.body);
    }
    return out;
  }
  if (const gamedata::AutoReply* a = gamedata::auto_reply(id)) {
    std::optional<Bytes> body2 = codec::encode_default(a->layout);
    out.kind = HandleResult::kFrame;
    out.frame = wire::frame(a->reply, body2.value_or(Bytes()));
    return out;
  }
  out.kind = HandleResult::kUnknown;
  return out;
}

bool caught_by_reply_chunks(const Raised& r) {
  return r.type == "struct.error" || r.type == "ValueError" || r.type == "IndexError" ||
         r.type == "UnicodeDecodeError" || r.type == "json.decoder.JSONDecodeError";
}

}
}
