# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.handlers import Ctx, register
from tcreborn.wire import blob


@register(0x7C40F0EC, "SAVE_UPLOAD")
def save_upload(ctx: Ctx, body: bytes):
    (length,) = struct.unpack_from(">I", body, 8)
    ctx.player.save_blob = body[12:12 + length]
    ctx.player.save()
    return 0x7FA0C5DE, bytes.fromhex("000001")


@register(0x9EAC6D16, "SAVE_READ")
def save_read(ctx: Ctx, body: bytes):


    return 0x74FAB4D9, blob(ctx.player.save_blob)
