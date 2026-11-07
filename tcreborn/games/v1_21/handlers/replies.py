# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import socket
import struct
import zlib

from tcreborn.games.v1_21.handlers import Ctx, register

_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "replies.json")
with open(_FILE) as _f:
    TEMPLATES: dict[str, dict] = json.load(_f)


PLAYER_U32 = {"bucks_be": "bucks", "cc_be": "cc", "field12_be": "field12", "level_be": "level",
              "xp_be": "xp"}


def fill(ctx: Ctx, body: bytes, fields: list) -> bytes:
    out = bytearray(body)
    for offset, kind in fields:
        if kind == "player_id_be":
            struct.pack_into(">Q", out, offset, ctx.player_id)
        elif kind == "player_id_le":
            struct.pack_into("<Q", out, offset, ctx.player_id)
        elif kind == "now_us_be":
            struct.pack_into(">Q", out, offset, ctx.now_us())
        elif kind in PLAYER_U32:
            struct.pack_into(">I", out, offset, getattr(ctx.player, PLAYER_U32[kind]))
        elif kind == "level2_be16":
            struct.pack_into(">H", out, offset, ctx.player.perk_points)
        elif kind == "ps4_ok_flag":
            out[offset] = 1
    return bytes(out)


def _activity_end(ctx: Ctx, request: bytes, template: bytes) -> bytes:
    from tcreborn.games.v1_21 import rewards
    activity, value = struct.unpack_from(">QI", request, 0x1C)
    p = ctx.player
    p.complete_activity(activity)
    if rewards.TABLES is not None:
        rewards.apply(rewards.TABLES, p, *rewards.activity_reward(rewards.TABLES, activity, value, p.level))
    p.save()
    return struct.pack(">BQ4I", 1, activity, p.level, p.xp, p.bucks, p.perk_points) + template[25:]


BUILDER_FACTOR = 100.0


def _mission_result(ctx: Ctx, request: bytes, template: bytes) -> bytes:
    from tcreborn.games.v1_21 import rewards
    from tcreborn.games.v1_21.handlers.mission import finish_mission
    mission = struct.unpack_from(">Q", request, 0x1C)[0] if len(request) >= 0x24 else 0
    p = ctx.player
    t = rewards.TABLES
    if t is not None:
        bucks = int(rewards._f32(t.f(rewards.BUCKS_BASE + p.level - 1) * BUILDER_FACTOR))
        xp = int(rewards._f32(t.f(rewards.XP_BASE + p.level - 1) * BUILDER_FACTOR))
        rewards.add_bucks(t, p, bucks)
        rewards.add_xp(t, p, xp)
    if mission:
        finish_mission(p, mission, 0, paid=False)
    p.save()
    from tcreborn.wire import u64_array

    return struct.pack(">H3IH", 0, p.level, p.xp, p.bucks, p.perk_points) + u64_array([0xFFFFFFFFFFFFFFFF]) \
        + struct.pack(">IQ", 0, 0)


REPLACE = {0x9FD2D3D4: _activity_end, 0xAE5D6365: _mission_result}


def _template_handler(req: int, t: dict):
    reply_type, body, fields = int(t["type"], 16), bytes.fromhex(t["body"]), t["fields"]

    def handler(ctx: Ctx, request: bytes):
        if req in REPLACE:
            return reply_type, REPLACE[req](ctx, request, body)
        return reply_type, fill(ctx, body, fields)
    return handler


PORTED = {0xF70D87D3, 0x0BE43657, 0xB7E9BB59, 0xB9BA9057, 0x115C657D, 0xF282A24C, 0xBF9B8FBD, 0xE6D82939,
          0x7C044567, 0x7932CE3E, 0xA7476B7D, 0x594BCC53, 0x82B2A771, 0xBBF8C44B, 0xF93D21DA, 0x740437D6, 0x2FC9E8A6, 0x88648CE9, 0x8B70F3EB, 0x96F6E078}


for _req, _t in TEMPLATES.items():
    if int(_req, 16) in PORTED:
        continue
    _name = _t["name"] if not _t["name"].startswith(("AUTO_", "0x")) else f"REQ_{_req[2:].upper()}"
    register(int(_req, 16), _name)(_template_handler(int(_req, 16), _t))


SERVICE_NAMES = [
    "AchievementsService", "ChaseService", "EntityStorage", "GroupService", "IDGenerator", "LFPService",
    "LoginService", "MetaService", "MissionService", "NPCService", "NetworkTimeService", "NewsService",
    "NotificationService", "PVPSessionService", "PlayerService", "PushService", "RMTService",
    "RecordsService", "RewardService", "RichPresence", "ScoresService", "SessionIDGenerator",
    "SessionService", "SkillService", "StatsService", "SummitService", "UGCService", "UserPresence",
    "WallService"]


def service_directory() -> dict[tuple[str, int], str]:
    body = bytes.fromhex(TEMPLATES["0x40bd5e2b"]["body"])
    by_crc = {zlib.crc32(f"{n}{i}".encode()): f"{n}{i}" for n in SERVICE_NAMES for i in range(64)}
    count = struct.unpack_from(">I", body, 17)[0]
    out = {}
    for k in range(count):
        ip, port, crc = struct.unpack_from(">4sHI", body, 21 + 10 * k)
        out[(socket.inet_ntoa(ip), port)] = by_crc.get(crc, f"{crc:#010x}")
    return out


SERVICE_BY_ADDR = service_directory()
