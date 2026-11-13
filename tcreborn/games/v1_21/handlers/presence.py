# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.wire import empty_array

PRESENCE = 0x49466F25


@register(PRESENCE, "PLAYER_UPDATE")
def player_update(ctx: Ctx, body: bytes):
    (n,) = struct.unpack_from(">I", body, 0x20)
    p = ctx.player
    a, b = struct.unpack_from(">QQ", body, 0x2D + n)
    if a not in (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF):
        p.ids = [a, b]
    pos = list(struct.unpack_from(">3I", body, 0x3E + n))
    if all(pos):
        p.position = pos
    direction = list(struct.unpack_from(">3I", body, 0x50 + n))
    if all(direction):
        p.direction = direction
    (p.zone,) = struct.unpack_from(">I", body, 0x78 + n)
    (spent,) = struct.unpack_from(">I", body, 0x60 + n)
    if spent and spent < p.update_charge:
        p.update_charge = spent
    elif spent and spent != p.update_charge and p.bucks >= spent - p.update_charge:
        p.bucks -= spent - p.update_charge
        p.update_charge = spent
    p.save()
    return 0xC8A3B105, b"\x00" + 2 * empty_array() + b"\x00"
