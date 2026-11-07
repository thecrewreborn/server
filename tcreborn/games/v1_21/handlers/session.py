# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21 import peers
from tcreborn.games.v1_21.handlers import Ctx, register


@register(0x8B70F3EB, "SESSION_UPDATE")
def session_update(ctx: Ctx, body: bytes):
    pid, x, y = struct.unpack_from(">Qff", body, 4)
    return 0xBDF8D019, peers.session_reply(pid, x, y)


@register(0x96F6E078, "PLAYERS_NEARBY")
def players_nearby(ctx: Ctx, body: bytes):
    pid = struct.unpack_from(">Q", body, 0)[0]
    x, y = struct.unpack_from(">ff", body, 8 + 12)
    return 0xCEA29EAA, peers.nearby_reply(pid, x, y)


@register(0xB228E43F, "SESSION_JOIN")
def session_join(ctx: Ctx, body: bytes):
    return 0x8DE2049D, peers.session_join_reply(body)
