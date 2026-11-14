#include "core/v1_21/replies.h"

#include <cstdio>
#include <unordered_map>

#include "core/core.h"
#include "core/v1_21/gamedata.h"
#include "core/v1_21/mission.h"
#include "core/v1_21/pack.h"
#include "core/v1_21/rewards.h"
#include "core/wire.h"

namespace tcr {
namespace v1_21 {
namespace replies {

namespace {

const uint32_t kPorted[] = {0xF70D87D3, 0x0BE43657, 0xB7E9BB59, 0xB9BA9057, 0x115C657D, 0xF282A24C, 0xBF9B8FBD,
                            0xE6D82939, 0x7C044567, 0x7932CE3E, 0xA7476B7D, 0x594BCC53, 0x82B2A771, 0xBBF8C44B,
                            0xF93D21DA, 0x740437D6, 0x2FC9E8A6, 0x88648CE9, 0x8B70F3EB, 0x96F6E078};
const uint32_t kReplace[] = {0x9FD2D3D4, 0xAE5D6365};

void put_le(Bytes& out, size_t offset, uint64_t v, size_t n) {
  for (size_t i = 0; i < n && offset + i < out.size(); i++) out[offset + i] = static_cast<uint8_t>(v >> (8 * i));
}

HandlerResult template_handler(Ctx& ctx, const Bytes&, const Registered& self) {
  const auto* t = static_cast<const gamedata::ReplyTemplate*>(self.data);
  Raised r;
  std::optional<Bytes> out = fill(ctx, t->body, t->fields, &r);
  if (!out) return HandlerResult::fail(r);
  return HandlerResult::reply(t->type, std::move(*out));
}

HandlerResult reply_of(uint32_t type, pack::Packer& pk) {
  if (pk.failed()) return HandlerResult::fail(pk.raised());
  return HandlerResult::reply(type, std::move(pk.out()));
}

HandlerResult activity_end(Ctx& ctx, const Bytes& request, const Registered& self) {
  const auto* t = static_cast<const gamedata::ReplyTemplate*>(self.data);
  Raised r;
  if (!pack::unpack_check(request, 0x1C, 12, &r)) return HandlerResult::fail(r);
  uint64_t activity = wire::get_u64(request.data() + 0x1C);
  int64_t value = wire::get_u32(request.data() + 0x24);
  Player& p = ctx.player;
  obj::Py py;
  p.complete_activity(py, activity);
  if (py.failed()) return HandlerResult::fail(py.raised());
  if (const rewards::Tables* tables = rewards::tables(*ctx.core)) {
    std::optional<std::pair<int64_t, int64_t>> reward = rewards::activity_reward(*tables, activity, value, p.level, &r);
    if (!reward) return HandlerResult::fail(r);
    std::pair<V, V> allowed =
        rewards::allow(py, *ctx.core, p, ctx.now_us(), obj::I(reward->first), obj::I(reward->second));
    rewards::apply(py, *tables, p, allowed.first, allowed.second, ctx.core->module_random());
    if (py.failed()) return HandlerResult::fail(py.raised());
  }
  if (std::optional<Raised> f = p.save()) return HandlerResult::fail(*f);
  pack::Packer pk;
  pk.B(1).Q(activity).I(p.level).I(p.xp).I(p.bucks).I(p.perk_points);
  if (t->body.size() > 25) pk.raw(Bytes(t->body.begin() + 25, t->body.end()));
  return reply_of(t->type, pk);
}

constexpr double BUILDER_FACTOR = 100.0;

HandlerResult mission_result(Ctx& ctx, const Bytes& request, const Registered& self) {
  const auto* t = static_cast<const gamedata::ReplyTemplate*>(self.data);
  Raised r;
  uint64_t mission = request.size() >= 0x24 ? wire::get_u64(request.data() + 0x1C) : 0;
  Player& p = ctx.player;
  obj::Py py;
  if (const rewards::Tables* tables = rewards::tables(*ctx.core)) {
    std::optional<double> b = tables->f(rewards::BUCKS_BASE + p.level - 1, &r);
    if (!b) return HandlerResult::fail(r);
    int64_t bucks = static_cast<int64_t>(rewards::f32(*b * BUILDER_FACTOR));
    std::optional<double> x = tables->f(rewards::XP_BASE + p.level - 1, &r);
    if (!x) return HandlerResult::fail(r);
    int64_t xp = static_cast<int64_t>(rewards::f32(*x * BUILDER_FACTOR));
    std::pair<V, V> allowed = rewards::allow(py, *ctx.core, p, ctx.now_us(), obj::I(xp), obj::I(bucks));
    rewards::add_bucks(py, *tables, p, allowed.second);
    rewards::add_xp(py, *tables, p, allowed.first, ctx.core->module_random());
    if (py.failed()) return HandlerResult::fail(py.raised());
  }
  if (mission) {
    if (!finish_mission(*ctx.core, p, mission, 0, ctx.now_us(), false, &r)) return HandlerResult::fail(r);
  }
  if (std::optional<Raised> f = p.save()) return HandlerResult::fail(*f);
  pack::Packer pk;
  pk.H(0).I(p.level).I(p.xp).I(p.bucks).H(p.perk_points);
  pk.raw(wire::u64_array({0xFFFFFFFFFFFFFFFFull})).I(0).Q(uint64_t(0));
  return reply_of(t->type, pk);
}

std::string upper_hex(uint32_t id) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08X", id);
  return buf;
}

}

std::optional<Bytes> fill(const Ctx& ctx, const Bytes& body,
                          const std::vector<std::pair<size_t, std::string>>& fields, Raised* raised) {
  Bytes out = body;
  const Player& p = ctx.player;
  obj::Py py;
  auto into = [&](size_t offset, char code, const V& v) {
    Bytes one;
    if (!py.pack(one, code, v)) return false;
    if (offset + one.size() > out.size()) {
      return py.raise("struct.error", "pack_into requires a buffer of at least " + std::to_string(offset + one.size()) +
                                          " bytes for packing " + std::to_string(one.size()) + " bytes at offset " +
                                          std::to_string(offset) + " (actual buffer size is " +
                                          std::to_string(out.size()) + ")");
    }
    std::copy(one.begin(), one.end(), out.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
  };
  for (const auto& f : fields) {
    size_t offset = f.first;
    const std::string& kind = f.second;
    bool ok = true;
    if (kind == "player_id_be") {
      ok = into(offset, 'Q', obj::U(ctx.player_id));
    } else if (kind == "player_id_le") {
      put_le(out, offset, ctx.player_id, 8);
    } else if (kind == "now_us_be") {
      ok = into(offset, 'Q', obj::I(ctx.now_us()));
    } else if (kind == "bucks_be") {
      ok = into(offset, 'I', p.bucks);
    } else if (kind == "cc_be") {
      ok = into(offset, 'I', obj::I(p.cc()));
    } else if (kind == "field12_be") {
      ok = into(offset, 'I', obj::I(p.field12));
    } else if (kind == "level_be") {
      ok = into(offset, 'I', obj::I(p.level));
    } else if (kind == "xp_be") {
      ok = into(offset, 'I', p.xp);
    } else if (kind == "level2_be16") {
      ok = into(offset, 'H', obj::I(p.perk_points));
    } else if (kind == "ps4_ok_flag") {
      if (offset < out.size()) out[offset] = 1;
    }
    if (!ok) {
      *raised = py.raised();
      return std::nullopt;
    }
  }
  return out;
}

const std::map<std::pair<std::string, int>, std::string>& service_by_addr() {
  static const std::map<std::pair<std::string, int>, std::string> table = [] {
    std::map<std::pair<std::string, int>, std::string> out;
    const gamedata::ReplyTemplate* t = gamedata::reply_template(0x40BD5E2B);
    if (!t) return out;
    static const char* const kNames[] = {
        "AchievementsService", "ChaseService", "EntityStorage", "GroupService", "IDGenerator", "LFPService",
        "LoginService", "MetaService", "MissionService", "NPCService", "NetworkTimeService", "NewsService",
        "NotificationService", "PVPSessionService", "PlayerService", "PushService", "RMTService",
        "RecordsService", "RewardService", "RichPresence", "ScoresService", "SessionIDGenerator",
        "SessionService", "SkillService", "StatsService", "SummitService", "UGCService", "UserPresence",
        "WallService"};
    std::unordered_map<uint32_t, std::string> by_crc;
    for (const char* n : kNames) {
      for (int i = 0; i < 64; i++) {
        std::string name = std::string(n) + std::to_string(i);
        by_crc[wire::crc32(reinterpret_cast<const uint8_t*>(name.data()), name.size())] = name;
      }
    }
    const Bytes& body = t->body;
    if (body.size() < 21) return out;
    uint32_t count = wire::get_u32(body.data() + 17);
    for (uint32_t k = 0; k < count && 21 + 10 * size_t(k) + 10 <= body.size(); k++) {
      const uint8_t* e = body.data() + 21 + 10 * k;
      char ip[16];
      std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", e[0], e[1], e[2], e[3]);
      int port = e[4] << 8 | e[5];
      uint32_t crc = wire::get_u32(e + 6);
      auto it = by_crc.find(crc);
      char hex[16];
      std::snprintf(hex, sizeof(hex), "0x%08x", crc);
      out[{ip, port}] = it != by_crc.end() ? it->second : std::string(hex);
    }
    return out;
  }();
  return table;
}

}

void register_replies(std::vector<Registered>& out) {
  for (const gamedata::ReplyTemplate& t : gamedata::reply_templates()) {
    bool ported = false;
    for (uint32_t id : replies::kPorted) ported = ported || id == t.request;
    if (ported) continue;
    Registered r;
    r.id = t.request;
    bool generic = t.name.compare(0, 5, "AUTO_") == 0 || t.name.compare(0, 2, "0x") == 0;
    r.name = generic ? "REQ_" + replies::upper_hex(t.request) : t.name;
    r.data = &t;
    if (t.request == replies::kReplace[0]) {
      r.fn = replies::activity_end;
    } else if (t.request == replies::kReplace[1]) {
      r.fn = replies::mission_result;
    } else {
      r.fn = replies::template_handler;
    }
    out.push_back(std::move(r));
  }
}

}
}
