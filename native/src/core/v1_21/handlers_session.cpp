#include <cstring>

#include "core/v1_21/handlers.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/peers.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

HandlerResult session_update(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised raised;
  if (!pack::unpack_check(body, 4, 16, &raised)) return HandlerResult::fail(raised);
  double x = pack::f32_at(body, 12), y = pack::f32_at(body, 16);
  std::optional<uint64_t> pid;
  Bytes out;
  if (!peers::acting_id(*ctx.core, ctx, &pid, &raised) || !peers::session_reply(*ctx.core, pid, x, y, &out, &raised)) {
    return HandlerResult::fail(raised);
  }
  return HandlerResult::reply(0xBDF8D019, std::move(out));
}

HandlerResult players_nearby(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised raised;
  if (!pack::unpack_check(body, 0, 8, &raised)) return HandlerResult::fail(raised);
  if (!pack::unpack_check(body, 8 + 12, 8, &raised)) return HandlerResult::fail(raised);
  double x = pack::f32_at(body, 20), y = pack::f32_at(body, 24);
  std::optional<uint64_t> pid;
  Bytes out;
  if (!peers::acting_id(*ctx.core, ctx, &pid, &raised) || !peers::nearby_reply(*ctx.core, pid, x, y, &out, &raised)) {
    return HandlerResult::fail(raised);
  }
  return HandlerResult::reply(0xCEA29EAA, std::move(out));
}

HandlerResult session_join(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised raised;
  Bytes out;
  if (!peers::session_join_reply(*ctx.core, body, &ctx, &out, &raised)) return HandlerResult::fail(raised);
  return HandlerResult::reply(0x8DE2049D, std::move(out));
}

void add(std::vector<Registered>& out, uint32_t id, const char* name, Handler fn) {
  Registered r;
  r.id = id;
  r.name = name;
  r.fn = fn;
  out.push_back(std::move(r));
}

}

void register_session(std::vector<Registered>& out) {
  add(out, 0x8B70F3EB, "SESSION_UPDATE", session_update);
  add(out, 0x96F6E078, "PLAYERS_NEARBY", players_nearby);
  add(out, 0xB228E43F, "SESSION_JOIN", session_join);
}

}
}
