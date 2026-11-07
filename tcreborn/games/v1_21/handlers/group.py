# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21 import factions, rewards
from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.games.v1_21.handlers.replies import TEMPLATES, fill

DAY = 86400
DAILY_BUCKS = 2040


def _template(req: int) -> tuple[int, bytes, list]:
    t = TEMPLATES[f"0x{req:08x}"]
    return int(t["type"], 16), bytes.fromhex(t["body"]), t["fields"]


def _slots(body: bytes) -> list[tuple[int, str, int, int]]:
    out, taken = [], set()
    for o in range(len(body) - 7):
        v = struct.unpack_from(">Q", body, o)[0]
        for g, rec in enumerate(factions.START):
            for off in (0x54, 0x58, 0x5C, 0x60, 0x64):
                if v == factions.field(rec, off) and o not in taken:
                    out.append((o, ">Q", g, off))
                    taken.update(range(o, o + 8))
    for o in range(len(body) - 3):
        if o in taken:
            continue
        v = struct.unpack_from(">I", body, o)[0]
        for g, rec in enumerate(factions.START):
            if v == factions.field(rec, 0x68):
                out.append((o, ">I", g, 0x68))
    return out


SLOTS = {req: _slots(bytes.fromhex(TEMPLATES[f"0x{req:08x}"]["body"])) for req in (0xA7476B7D, 0x594BCC53, 0x82B2A771)}
OWN_BLOCK = {0xA7476B7D: 26, 0x594BCC53: 54, 0x82B2A771: 53}


def _status(ctx: Ctx, req: int, group_at: int, joined_at: int, unset_at: int | None) -> tuple[int, bytes]:
    rtype, body, fields = _template(req)
    p = ctx.player
    out = bytearray(fill(ctx, body, fields))
    groups = factions.load(ctx.now_us() // 1_000_000)
    for o, fmt, g, off in SLOTS[req]:
        struct.pack_into(fmt, out, o, factions.field(groups[g], off))
    if 1 <= p.group <= len(groups):
        rec = groups[p.group - 1]
        struct.pack_into(">12I", out, OWN_BLOCK[req], *(factions.field(rec, off) for off in range(0x6C, 0x9C, 4)))
    out[group_at] = p.group
    struct.pack_into(">Q", out, joined_at, p.group_joined if p.group else 0)
    if unset_at is not None:
        struct.pack_into(">I", out, unset_at, 0 if p.group else 0xFFFFFFFF)
    return rtype, bytes(out)


@register(0x7932CE3E, "GROUP_JOIN")
def group_join(ctx: Ctx, body: bytes):
    body = body.ljust(0x1E, b"\0")
    p, now = ctx.player, ctx.now_us()
    ok = body[0x1C] != p.group
    if ok:
        p.group, p.group_joined = body[0x1C], now
        p.save()
    return 0x9FD2A05A, struct.pack(">BQBQQII", ok, ctx.player_id, p.group, p.group_joined if p.group else now, now,
                                   0, 0xFFFFFFFF)


@register(0xA7476B7D, "DAILY_REWARD")
def daily_reward(ctx: Ctx, body: bytes):
    p = ctx.player
    now = ctx.now_us() // 1_000_000
    bucks = 0
    if p.daily_last == 0 or now - p.daily_last >= DAY:
        p.daily_last = now
        bucks = DAILY_BUCKS if p.group else 0
        if rewards.TABLES is not None:
            rewards.add_bucks(rewards.TABLES, p, bucks)
        else:
            p.bucks += bucks
        p.save()
    rtype, body = _status(ctx, 0xA7476B7D, 82, 91, 103)
    out = bytearray(body)
    struct.pack_into(">I", out, 307, bucks)
    return rtype, bytes(out)


@register(0x594BCC53, "GROUP_STATUS")
def group_status(ctx: Ctx, body: bytes):
    return _status(ctx, 0x594BCC53, 9, 18, 30)


@register(0x82B2A771, "GROUP_STATUS2")
def group_status2(ctx: Ctx, body: bytes):
    return _status(ctx, 0x82B2A771, 8, 17, 29)
