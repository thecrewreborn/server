# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.wire import blob, serializer_list


@register(0x2D147598, "NOTIF_POLL")
def notif_poll(ctx: Ctx, body: bytes):

    return 0xD2F92F15, struct.pack(">BI", 1, 30000) + blob(serializer_list([]))
