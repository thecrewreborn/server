# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.handlers import Ctx, register
from tcreborn.player import STAT_COUNT
from tcreborn.wire import T_ARRAY, blob, empty_array, frame


def read_array(body: bytes, pos: int, item_size: int) -> tuple[list[bytes], int]:
    (size,) = struct.unpack_from(">I", body, pos)
    (count,) = struct.unpack_from(">I", body, pos + 4 + 12)
    start = pos + 4 + 16
    return [body[start + i * item_size:start + (i + 1) * item_size] for i in range(count)], pos + 4 + size


@register(0x2E0D6D99, "STATS_WRITE")
def stats_write(ctx: Ctx, body: bytes):


    stats, pos = read_array(body, 8, 8)
    records, _ = read_array(body, pos, 24)
    for item in stats:
        ctx.player.set_stat(*struct.unpack(">II", item))
    for item in records:
        ctx.player.set_record_field(*struct.unpack(">QQII", item))
    ctx.player.save()
    return 0xF63E32AE, b"\x01"


@register(0x812D4694, "STATS_GET")
def stats_get(ctx: Ctx, body: bytes):

    p = ctx.player
    records = b"".join(struct.pack(">QQ", a, b) + struct.pack(">15I", *v) for (a, b), v in p.records.items())
    return 0xFE559B32, (struct.pack(">Q", ctx.player_id) + struct.pack(f">{STAT_COUNT}I", *p.stats[:STAT_COUNT])
                        + blob(frame(T_ARRAY, struct.pack(">I", len(p.records)) + records)) + empty_array())
