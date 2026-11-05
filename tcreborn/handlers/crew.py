# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import itertools
import struct
import threading

from tcreborn.handlers import Ctx, register

GROUP_BASE = 0x0000000100000000
_ids = itertools.count(GROUP_BASE + 1)
_lock = threading.Lock()


@register(0x9868D8CE, "PLAYER_SET_GROUP")
def set_group(ctx: Ctx, body: bytes):
    body = body.ljust(0x24, b"\0")
    ctx.crew_group = struct.unpack_from(">Q", body, 0x1C)[0]
    return 0x3069959F, struct.pack(">BH", 1, 0)


@register(0x7FA3E7E6, "GROUP_REGISTER")
def register_group(ctx: Ctx, body: bytes):
    with _lock:
        gid = next(_ids)
    return 0x10B92C81, struct.pack(">BQ", 1, gid)


@register(0x80A06106, "GROUP_KEEP_ALIVE")
def keep_alive(ctx: Ctx, body: bytes):
    return 0xA3EFE71A, b"\x01"


@register(0x33065129, "GROUP_LEAVE")
def leave(ctx: Ctx, body: bytes):
    return 0xFAFBF944, b"\x01"
