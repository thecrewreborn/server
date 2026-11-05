# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import socket
import struct

from tcreborn import cars
from tcreborn.handlers import Ctx, register
from tcreborn.wire import T_ARRAY, blob, empty_array, frame, serializer_list

NONE = 0xFFFFFFFFFFFFFFFF
SERVICE_PORT = 4000
SERVICES = [
    "SessionService", "NotificationService", "NetworkTimeService", "GroupService", "SessionIDGenerator",
    "EntityStorage", "MissionService", "NPCService", "PlayerService", "PVPSessionService", "NewsService",
    "RecordsService", "SkillService", "AchievementsService", "WallService", "StatsService", "MetaService",
    "ScoresService", "LoginService", "RichPresence", "SummitService", "LFPService", "RMTService", "UGCService",
    "ChaseService", "UserPresence", "RewardService", "PushService", "IDGenerator"]
RULE_ANY_INSTANCE = 0xD4150057


def service_address(index: int) -> tuple[str, int]:
    return f"10.0.0.{index + 1}", SERVICE_PORT


SERVICE_BY_ADDR = {service_address(i): f"{name}0" for i, name in enumerate(SERVICES)}


@register(0x40BD5E2B, "QUERYSERVICES")
def query_services(ctx: Ctx, body: bytes):
    entries = b"".join(socket.inet_aton(service_address(i)[0]) + struct.pack(">HI", SERVICE_PORT, crc32(f"{n}0"))
                       for i, n in enumerate(SERVICES))
    return 0xA23A6CBD, b"\x00" + blob(frame(T_ARRAY, struct.pack(">I", len(SERVICES)) + entries))


@register(0x27C711C5, "SCALABILITY_RULES")
def scalability_rules(ctx: Ctx, body: bytes):
    return 0xBFA116CC, blob(serializer_list([frame(RULE_ANY_INSTANCE, blob(n.encode())) for n in SERVICES]))


@register(0xA1155286, "GAME_CONTENT")
def game_content(ctx: Ctx, body: bytes):
    return 0x6A8BF114, struct.pack(">BQ", 1, 0) + blob(b"") + struct.pack(">I", 0)


@register(0x368D58C9, "SESSION_UIDS")
def session_uids(ctx: Ctx, body: bytes):
    return 0xC7AFE0D4, struct.pack(">BQI", 1, 0x0000000100000000, 0x10000)


@register(0x720CC96E, "EXPORT_REVISION")
def export_revision(ctx: Ctx, body: bytes):
    revisions = struct.unpack_from(">4I", body.ljust(21, b"\0"), 5)
    return 0x6F699B68, struct.pack(">BBI4I", 0, 0, 0, *revisions)


@register(0x00FC9B0F, "NOTIF_SUBSCRIBE")
def notif_subscribe(ctx: Ctx, body: bytes):
    poll_ms, retry_ms, retries, timeout_ms = 24286, 1500, 4, 30000
    return 0x1B7A2D13, struct.pack(">B7I", 1, 0, poll_ms, 0, 0, retry_ms, retries, timeout_ms)


@register(0x006A4A0D, "REQ_006A4A0D")
def req_006a4a0d(ctx: Ctx, body: bytes):
    return 0x94A88BE4, struct.pack(">BI", 1, 239833)


@register(0x0A49C434, "REQ_0A49C434")
def req_0a49c434(ctx: Ctx, body: bytes):
    return 0x8848AA05, struct.pack(">I", 0) + empty_array() + b"\x01"


@register(0x7FAC9E06, "REQ_7FAC9E06")
def req_7fac9e06(ctx: Ctx, body: bytes):
    return 0xB9056E7B, struct.pack(">I", 0) + empty_array() + b"\x01"


@register(0x33450274, "REQ_33450274")
def req_33450274(ctx: Ctx, body: bytes):
    return 0x9442E7EB, struct.pack(">BIIB", 1, 30000, 0, 1)


@register(0x5ABA06B7, "REQ_5ABA06B7")
def req_5aba06b7(ctx: Ctx, body: bytes):
    return 0x99803EB2, empty_array()


@register(0x6858A287, "REQ_6858A287")
def req_6858a287(ctx: Ctx, body: bytes):
    entries = b"".join(struct.pack(">BII", i, 30000, 0) for i in range(3))
    return 0x383F5120, blob(frame(T_ARRAY, struct.pack(">I", 3) + entries))


@register(0x8ED4B082, "REQ_8ED4B082")
def req_8ed4b082(ctx: Ctx, body: bytes):
    content_ids = 0
    entry = struct.pack(">QIBI", 0, content_ids, 0, 0xFFFFFFFF)
    return 0x3699E221, blob(frame(T_ARRAY, struct.pack(">I", 1) + entry)) + b"\x01"


@register(0xA4E5124D, "REQ_A4E5124D")
def req_a4e5124d(ctx: Ctx, body: bytes):
    season_start_us, season_end_us, stamp_us = 1448240400000000, 1480294800000000, 1710878615437253
    out = struct.pack(">HBQBQB8I", 0, 0, NONE, 0, NONE, 0, *([0] * 8))
    out += struct.pack(">BHBBBBQQBIQQBBBQ", 6, 2391, 0, 1, 0, 1, NONE, NONE, 0, 0, season_start_us, season_end_us,
                       3, 20, 0, stamp_us)
    out += empty_array() + struct.pack(">BIBQ", 1, 0, 4, NONE) + empty_array() * 2 + struct.pack(">Q", NONE)
    out += empty_array() * 3 + struct.pack(">BI", 3, 55001) + empty_array() + struct.pack(">III", 0, 189, 0xFFFFFFFF)
    return 0x0AB8EE75, out


@register(0xAD26AF59, "MISSION_STATE")
def mission_state(ctx: Ctx, body: bytes):
    from tcreborn.handlers import group
    _, head = group._status(ctx, 0x594BCC53, 9, 18, 30)
    return 0xB41B0F92, head + struct.pack(">I", 1) + bytes(16) + empty_array() + bytes(4)


def crc32(s: str) -> int:
    import zlib
    return zlib.crc32(s.encode())


STATUS_LAYOUT = {
    0x594BCC53: dict(size=302, reply=0xC0CFC1C9, ok=0, player_be=1, now=(10,), standings=34, numbers=(102,)),
    0x82B2A771: dict(size=301, reply=0x3D3D4841, ok=None, player_be=0, now=(9,), standings=33, numbers=(101,)),
    0xA7476B7D: dict(size=545, reply=0xA0F5C8C7, ok=0, player_le=74, now=(83, 319), standings=6,
                     numbers=(107, 327)),
}
GROUP_FIELDS = (0x54, 0x58, 0x5C, 0x60, 0x64)
WEEK_US = 7 * 24 * 3600 * 1_000_000


def status_slots(req: int) -> list[tuple[int, str, int, int]]:
    lay = STATUS_LAYOUT[req]
    out = [(lay["standings"] + 4 * g, ">I", g, 0x68) for g in range(5)]
    for base in lay["numbers"]:
        out += [(base + 40 * f + 8 * g, ">Q", g, off) for f, off in enumerate(GROUP_FIELDS) for g in range(5)]
    return out


def status_template(req: int) -> dict:
    lay = STATUS_LAYOUT[req]
    out = bytearray(lay["size"])
    fields = []
    if lay["ok"] is not None:
        fields.append([lay["ok"], "ps4_ok_flag"])
    if "player_be" in lay:
        fields.append([lay["player_be"], "player_id_be"])
    if "player_le" in lay:
        fields.append([lay["player_le"], "player_id_le"])
    fields += [[o, "now_us_be"] for o in lay["now"]]
    if req == 0xA7476B7D:
        fields.append([2, "bucks_be"])
        out[1] = 1
        struct.pack_into(">I", out, 315, 6)
        struct.pack_into(">3HQ", out, 531, 14, 2, 7, WEEK_US)
    return {"type": f"0x{lay['reply']:08x}", "body": bytes(out).hex(), "fields": fields}


STATUS_SLOTS = {req: status_slots(req) for req in STATUS_LAYOUT}
TEMPLATES = {f"0x{req:08x}": status_template(req) for req in STATUS_LAYOUT}

PLAYER_U32 = {"bucks_be": "bucks", "cc_be": "crew_credits", "field12_be": "field12", "level_be": "level",
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
        elif kind == "ps4_ok_flag":
            out[offset] = 1
    return bytes(out)


@register(0x9FD2D3D4, "ACTIVITY_END")
def activity_end(ctx: Ctx, body: bytes):
    from tcreborn import rewards
    activity, value = struct.unpack_from(">QI", body.ljust(0x28, b"\0"), 0x1C)
    p = ctx.player
    p.complete_activity(activity)
    if rewards.TABLES is not None:
        rewards.apply(rewards.TABLES, p, *rewards.activity_reward(rewards.TABLES, activity, value, p.level))
    p.save()
    return 0xB83CE3A5, (struct.pack(">BQ4I", 1, activity, p.level, p.xp, p.bucks, p.perk_points)
                        + empty_array() * 2 + struct.pack(">H", 0) + empty_array() + struct.pack(">I", 0))


BUILDER_FACTOR = 100.0


@register(0xAE5D6365, "MISSION_RESULT")
def mission_result(ctx: Ctx, body: bytes):
    from tcreborn import rewards
    from tcreborn.handlers.mission import finish_mission
    from tcreborn.wire import u64_array
    mission = struct.unpack_from(">Q", body, 0x1C)[0] if len(body) >= 0x24 else 0
    p = ctx.player
    t = rewards.TABLES
    if t is not None:
        rewards.add_bucks(t, p, int(rewards._f32(t.f(rewards.BUCKS_BASE + p.level - 1) * BUILDER_FACTOR)))
        rewards.add_xp(t, p, int(rewards._f32(t.f(rewards.XP_BASE + p.level - 1) * BUILDER_FACTOR)))
    if mission:
        finish_mission(p, mission, 0, paid=False)
    p.save()
    return 0x278DF851, (struct.pack(">H3IH", 0, p.level, p.xp, p.bucks, p.perk_points) + u64_array([NONE])
                        + struct.pack(">IQ", 0, 0))


def region_reward_without_car() -> bytes:
    from tcreborn.handlers.progress import REWARD_STATE
    return (struct.pack(">HQ", 0xFFFF, NONE) + cars.array(1, cars.with_state(cars.EMPTY_CONFIG, REWARD_STATE))
            + cars.array(26, bytes(52)))
