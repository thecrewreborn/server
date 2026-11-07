# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21 import rewards, summit
from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.games.v1_21.player import NO_ID
from tcreborn.wire import T_ARRAY, blob, empty_array, frame, u64_array

WINDOW_2017 = (0x554F34B42AC00, 0x560F920FB0C00)
WINDOW_2023 = (0x60AC9080C6C00, 0x60D248749AC00)


def _head(status: int, p) -> bytes:
    return struct.pack(">HIIIHBH4B", status, p.level, p.xp, p.bucks, p.perk_points, 0x28, 0, 0, 0, 0, 1)


def _block(now: int, tail: int, last: int) -> bytes:
    return struct.pack(">QQBIQQ3BQ", now, NO_ID, 0, 0, *WINDOW_2023, 3, 0x14, tail, last) + empty_array()


def _cur(ctx: Ctx) -> int:
    cur = summit.current(ctx.now_us())
    summit.sync(ctx.player, cur)
    return cur


@register(0x0BE43657, "SUMMIT_ENTRY")
def summit_entry(ctx: Ctx, body: bytes):
    body = body.ljust(0x50, b"\0")
    p = ctx.player
    if summit.enter(p, _cur(ctx)):
        p.save()
    return 0x5BFA99B8, (struct.pack(">HBIQQBIQQ3BQ", 0, 1, p.bucks, ctx.now_us(), NO_ID, 0, 0, *WINDOW_2017,
                                    3, 0x1E, 3, 0) + empty_array() + struct.pack(">BI", 1, 0))


@register(0xB7E9BB59, "SUMMIT_EVENT")
def summit_event(ctx: Ctx, body: bytes):
    body = body.ljust(0x50, b"\0")
    mission, = struct.unpack_from(">Q", body, 0x1C)
    score, = struct.unpack_from(">I", body, 0x25)
    p = ctx.player
    cur = _cur(ctx)
    slot = summit.event_slot(mission, cur)
    ok = slot is not None and summit.state(p)["entered"]
    if ok:
        xp, bucks = summit.event_reward(cur)
        if rewards.TABLES is not None:
            rewards.apply(rewards.TABLES, p, xp, bucks)
        summit.post_score(p, slot, score)
        p.save()
    now = ctx.now_us()
    tail = u64_array([NO_ID]) + struct.pack(">III", 0, 0, 0x65) if ok else \
        empty_array() + struct.pack(">III", 0, 0xFFFFFFFF, 0x65)
    return 0xCF266AB3, (_head(0 if ok else 1, p) + _block(now, 0, now) + struct.pack(">BIBI", 0, 0x113, 1, 0x1E48C)
                        + tail)


@register(0xB9BA9057, "SUMMIT_RESULT")
def summit_result(ctx: Ctx, body: bytes):
    body = body.ljust(0x50, b"\0")
    rank, = struct.unpack_from(">I", body, 0x1C)
    p = ctx.player
    cur = _cur(ctx)
    s = summit.state(p)
    if rewards.TABLES is not None:
        rewards.add_bucks(rewards.TABLES, p, 1)
    else:
        p.bucks += 1
    snapshot = struct.pack(">4B", *s["tokens"], s["next_kind"])
    entered = s["entered"]
    summit.result(p, rank, cur)
    p.save()
    return 0xCCF7FCFC, (struct.pack(">HIBH", 0, p.bucks, 0x28, 0) + snapshot + _block(ctx.now_us(), 3, 0)
                        + struct.pack(">BIBIBBII", 1, 0x1CB, 1, 0x258, entered, 0, 0, 0x35))


@register(0x115C657D, "SUMMIT_STATUS")
def summit_status(ctx: Ctx, body: bytes):
    body = body.ljust(0x50, b"\0")
    p = ctx.player
    _cur(ctx)
    s = summit.state(p)
    s["value"] = struct.unpack_from(">I", body, 0x1E)[0]
    s["results"] += 1
    p.save()
    now = ctx.now_us()
    return 0xE4EA417B, (_head(0, p) + _block(now, 0, now)
                        + struct.pack(">BIBIIBBIIIQ", 1, 0x31, 1, 0x2D7C8, 0, s["entered"], 0, 0, 3, 0x172C,
                                      ctx.player_id))


@register(0xD14A2746, "SUMMIT_SCORES")
def summit_scores(ctx: Ctx, body: bytes):
    p = ctx.player
    _cur(ctx)
    scores = sorted((int(k), v) for k, v in summit.state(p)["scores"].items())
    if not scores:
        return 0x262AF119, struct.pack(">BII", 1, 0xFFFFFFFF, 0) + empty_array() + b"\x01"
    entries = struct.pack(">I", len(scores)) + b"".join(struct.pack(">BQQ", k, v, 1) for k, v in scores)
    return 0x262AF119, struct.pack(">BII", 4, 0, 1) + blob(frame(T_ARRAY, entries)) + b"\x01"


@register(0xF282A24C, "SAVE_IDS")
def save_ids(ctx: Ctx, body: bytes):
    body = body.ljust(0x50, b"\0")
    ctx.player.save_ids = list(struct.unpack_from(">6Q", body, 0x1C))
    ctx.player.save()
    return 0x829BB4D2, b"\x01\x00\x00"
