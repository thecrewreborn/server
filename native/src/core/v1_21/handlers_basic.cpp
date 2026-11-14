#include "core/v1_21/handlers.h"
#include "core/v1_21/pack.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {

namespace {

constexpr size_t MAX_SAVE_BLOB = 4096;

HandlerResult save_upload(Ctx& ctx, const Bytes& body, const Registered&) {
  Raised raised;
  uint32_t length = 0;
  if (!pack::unpack_u32(body, 8, &length, &raised)) return HandlerResult::fail(raised);
  size_t a = std::min<size_t>(12, body.size()), b = std::min<size_t>(12 + static_cast<size_t>(length), body.size());
  Bytes data(body.begin() + a, body.begin() + std::max(a, b));
  if (data.size() <= MAX_SAVE_BLOB) {
    ctx.player.save_blob = std::move(data);
    if (std::optional<Raised> failed = ctx.player.save()) return HandlerResult::fail(*failed);
  }
  return HandlerResult::reply(0x7FA0C5DE, Bytes{0x00, 0x00, 0x01});
}

HandlerResult save_read(Ctx& ctx, const Bytes&, const Registered&) {
  return HandlerResult::reply(0x74FAB4D9, wire::blob(ctx.player.save_blob));
}

HandlerResult notif_poll(Ctx&, const Bytes&, const Registered&) {
  Bytes out{1};
  wire::put_u32(out, 30000);
  Bytes list = wire::blob(wire::serializer_list({}));
  out.insert(out.end(), list.begin(), list.end());
  return HandlerResult::reply(0xD2F92F15, std::move(out));
}

HandlerResult ubi_ticket(Ctx& ctx, const Bytes&, const Registered&) {
  Bytes out;
  wire::put_u16(out, 0);
  Bytes empty = wire::empty_array();
  out.insert(out.end(), empty.begin(), empty.end());
  wire::put_u32(out, static_cast<uint32_t>(ctx.player.cc()));
  out.insert(out.end(), empty.begin(), empty.end());
  Bytes list = wire::blob(wire::serializer_list({}));
  out.insert(out.end(), list.begin(), list.end());
  return HandlerResult::reply(0x515A56AB, std::move(out));
}

void add(std::vector<Registered>& out, uint32_t id, const char* name, Handler fn) {
  Registered r;
  r.id = id;
  r.name = name;
  r.fn = fn;
  out.push_back(std::move(r));
}

}

void register_entity(std::vector<Registered>& out) {
  add(out, 0x7C40F0EC, "SAVE_UPLOAD", save_upload);
  add(out, 0x9EAC6D16, "SAVE_READ", save_read);
}

void register_notify(std::vector<Registered>& out) { add(out, 0x2D147598, "NOTIF_POLL", notif_poll); }

void register_ticket(std::vector<Registered>& out) { add(out, 0x0396B37E, "UBI_TICKET", ubi_ticket); }

}
}
