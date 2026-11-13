# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21 import rewards
from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.games.v1_21.handlers.mission import _rating
from tcreborn.wire import T_ARRAY, blob, empty_array, frame

NO_ID = 0xFFFFFFFFFFFFFFFF
HEAT_CAP = 100000.0


WANTED_CAP = 100000
CHASE_CAR = (0x2522F39, 0x794C74A)


def chase(p, escaped: bool, wanted: int, start: int, now: int) -> int:
    h = min(p.heat, HEAT_CAP)
    wanted = min(wanted, WANTED_CAP)
    xp = int(wanted * 0.4) + 100 + int(0.05 * h)
    if escaped:
        bonus, fine = int(0.2 * h), 0
        bucks = int(wanted * 0.5) + 500 + bonus
        grown = ((now - start) % (1 << 64)) * 0.5 + wanted * 0.2
        p.heat = max(float(int(grown + h)), 1.0)
    else:
        bonus = 0
        fine = max(-500 - int(0.2 * h) - int(wanted * 0.5), -50000)
        bucks = fine
        p.heat = 1.0
    xp, bucks = rewards.allow(p, now * 1_000_000, xp, bucks)
    if escaped:
        bonus = min(bonus, bucks)
    if rewards.TABLES is not None:
        rewards.add_bucks(rewards.TABLES, p, bucks)
        rewards.add_xp(rewards.TABLES, p, xp)
    else:
        p.bucks, p.xp = max(0, p.bucks + bucks), p.xp + xp
    return (bonus + fine) & 0xFFFFFFFF


@register(0xF64F66EB, "CHASE_REWARD")
def chase_reward(ctx: Ctx, body: bytes):
    body = body.ljust(0x52, b"\0")
    p = ctx.player
    start, wanted = struct.unpack_from(">QI", body, 0x34)
    value = chase(p, bool(body[0x41]), wanted, start, ctx.now_us() // 1_000_000)
    if p.chase[0] == NO_ID:
        p.chase = list(CHASE_CAR)
    p.save()
    ratings = blob(frame(T_ARRAY, struct.pack(">I", 1) + _rating(ctx.player_id)))
    out = struct.pack(">H4I", 0, p.level, p.xp, p.bucks, p.perk_points)
    out += struct.pack(">QBQBQ", p.chase[0], 0, p.chase[1], 0, 0) + empty_array()
    out += struct.pack(">Q", NO_ID) + blob(frame(T_ARRAY, struct.pack(">I", 10))) + empty_array()
    out += struct.pack(">BQB5I", 0, NO_ID, 0, 0x8B3, 0, 300, value, 0x68) + empty_array()
    out += struct.pack(">I", 0) + 2 * empty_array() + 2 * ratings + struct.pack(">HB", 0, 0)
    return 0x2C81736C, out
