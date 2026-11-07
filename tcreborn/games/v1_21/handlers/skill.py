# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21 import rewards
from tcreborn.games.v1_21.handlers import Ctx, register


SKILL_CAP = 100000


@register(0xF16252BC, "SKILL_RESULT")
def skill_result(ctx: Ctx, body: bytes):


    (value,) = struct.unpack_from(">I", body, 0x1C)
    p = ctx.player
    delta = value - (p.skill_last if p.skill_last <= value else 0)
    ok = delta <= SKILL_CAP
    if ok:
        if rewards.TABLES is not None:
            rewards.add_bucks(rewards.TABLES, p, delta)
        else:
            p.bucks += delta
        p.skill_last = value
    else:
        p.skill_last = 0
    p.save()
    return 0xCD89472D, struct.pack(">BIH", ok, ctx.player.bucks, 0)


@register(0x37D6825B, "MAP_STATE")
def map_state(ctx: Ctx, body: bytes):


    (count,) = struct.unpack_from(">I", body, 0x1C + 4 + 12)
    if count < 11:
        ctx.player.map_state = list(struct.unpack_from(f">{count}Q", body, 0x1C + 4 + 16))
        ctx.player.save()
    return 0x49440B69, struct.pack(">BH", 1, 0)
