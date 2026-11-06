# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.handlers import Ctx, register
from tcreborn.wire import blob, empty_array, serializer_list


@register(0x0396B37E, "UBI_TICKET")
def ubi_ticket(ctx: Ctx, body: bytes):
    return 0x515A56AB, (struct.pack(">H", 0) + empty_array() + struct.pack(">I", ctx.player.cc) + empty_array()
                        + blob(serializer_list([])))
