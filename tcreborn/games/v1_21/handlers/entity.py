# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.wire import blob

MAX_SAVE_BLOB = 4096


@register(0x7C40F0EC, "SAVE_UPLOAD")
def save_upload(ctx: Ctx, body: bytes):
    (length,) = struct.unpack_from(">I", body, 8)
    data = body[12:12 + length]
    if len(data) <= MAX_SAVE_BLOB:
        ctx.player.save_blob = data
        ctx.player.save()
    return 0x7FA0C5DE, bytes.fromhex("000001")


@register(0x9EAC6D16, "SAVE_READ")
def save_read(ctx: Ctx, body: bytes):


    return 0x74FAB4D9, blob(ctx.player.save_blob)
