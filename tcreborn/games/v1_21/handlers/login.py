# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21 import cars, summit
from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.wire import T_ARRAY, blob, empty_array, frame, serializer_list, u64_array

GETPLAYER_REPLY_FIELDS = [
    "u8:ok",
    "u64:player_id", "u64:player_b", "u64:player_c",
    "guid:profile_id",
    "str:name",
    "u8", "u8", "u32", "u32", "u32",
    "array",
    "u32", "u16", "u64", "u32", "u64", "u64",
    "f32", "f32", "f32", "f32", "f32", "f32",
    "array", "array", "array", "array", "array", "array", "array",
    "u32", "u8", "u8x30", "u32", "u32", "u32",
    "array",
    "u8", "u16", "u8", "u8", "u8", "u8",
    "u64", "u64", "u8", "u32", "u64", "u64", "u8", "u8", "u8", "u64", "array", "u8", "u32",
    "str", "u32", "u64",
    "array",
    "u64", "u64", "u64", "u64", "u64", "u64", "u64", "u8", "u64", "u8",
    "u64",
    "array", "array",
    "str", "u32", "u16", "u8",
]


def _content_file(revision: int = 0, data: bytes = b"", extra: int = 0) -> bytes:
    return struct.pack(">Q", revision) + blob(data) + struct.pack(">I", extra)


def _archetype_files(body: bytes) -> list[tuple[bytes, int]]:
    count = struct.unpack_from(">I", body, 16)[0]
    files, o = [], 20
    for _ in range(count):
        n = struct.unpack_from(">I", body, o)[0]
        files.append((body[o + 4:o + 4 + n], struct.unpack_from(">Q", body, o + 4 + n)[0]))
        o += 12 + n
    return files


@register(0xCD3B60F2, "GAME_ARCHETYPES")
def archetypes(ctx: Ctx, body: bytes):


    try:
        files = _archetype_files(body)
    except struct.error:
        files = [(b"", 0), (b"", 0)]
    entries = b"".join(blob(name) + b"\x00" + _content_file(h) for name, h in files)
    return 0x44A84B08, b"\x01" + blob(frame(T_ARRAY, struct.pack(">I", len(files)) + entries))


@register(0x036D7773, "LOGIN_QUEUE")
def login_queue(ctx: Ctx, body: bytes):
    return 0x9152C10D, struct.pack(">IQ", 0, 0)


NET_CONFIG = {"Punch": {"ALLOW_PLAY_WITHOUT_PUNCH": "1"}}
T_CONFIG_SECTION = 0x5999B98B
T_CONFIG_VALUE = 0xBA2D37DC


def _str(s: str) -> bytes:
    return blob(s.encode())


@register(0x9AA22640, "NET_CONFIG_VALUES")
def net_config(ctx: Ctx, body: bytes):
    sections = [frame(T_CONFIG_SECTION, _str(name) + blob(serializer_list(
                    [frame(T_CONFIG_VALUE, _str(k) + _str(v)) for k, v in values.items()])))
                for name, values in NET_CONFIG.items()]
    return 0x8A35ECD9, b"\x01" + blob(serializer_list(sections))


@register(0xB1F54E5C, "PROBING_TIME")
def probing_time(ctx: Ctx, body: bytes):

    return 0x8386FE97, body[:8] + struct.pack(">Q", ctx.client_now_us())


NO_ID = 0xFFFFFFFFFFFFFFFF
START_DATE = b"20150101-120000"


NEW_PLAYER_FIELDS = {
    0: 1, 3: NO_ID, 6: 1, 7: 1, 14: NO_ID, 16: NO_ID, 17: NO_ID, 38: 1, 43: 3, 44: NO_ID, 45: NO_ID,
    48: NO_ID, 49: NO_ID, 50: 0xFF, 51: 0xFF, 52: 3, 55: 1, 57: START_DATE,
    61: NO_ID, 62: NO_ID, 63: NO_ID, 64: NO_ID, 65: NO_ID, 66: NO_ID, 67: NO_ID, 69: 0xFF,
    74: START_DATE, 77: 1,
}
GETPLAYER_SLOTS = 250


@register(0x589F57FB, "GET_PLAYER")
def get_player(ctx: Ctx, body: bytes):
    values = dict(NEW_PLAYER_FIELDS)
    p = ctx.player
    p.update_charge = 0
    values.update({7: p.level, 8: p.xp, 9: p.bucks, 10: p.cc, 12: p.field12, 13: p.perk_points, 15: p.zone, 16: p.ids[0], 17: p.ids[1],
                   **{18 + i: w for i, w in enumerate(p.position + p.direction)}})
    if p.progress:
        values[30] = u64_array(p.progress)
    if p.missions_done:
        values[25] = u64_array(p.missions_done)
    if p.map_state:
        values[27] = u64_array(p.map_state)
    if p.progress or p.map_state or p.missions_done:


        values[77] = 0
    if p.abilities:
        values[26] = u64_array(p.abilities)
    if p.rewarded:
        values[28] = u64_array(p.rewarded)
    if p.table_bits:
        values[29] = u64_array(p.table_bits)
    values[33] = bytes(p.perks)
    summit.sync(p, summit.current(ctx.now_us()))
    s = summit.state(p)

    values.update({38: s["threshold"], 39: s["last_kind"], 40: s["tokens"][0], 41: s["tokens"][1],
                   42: s["tokens"][2], 43: s["next_kind"], 55: 0 if s["entered"] else 1})
    values.update({61 + i: v for i, v in enumerate(p.save_ids)})
    values[67], values[69] = p.chase
    values.update({1: ctx.player_id, 5: ctx.player_name.encode(), 71: ctx.player_id,
                   73: blob(frame(T_ARRAY, struct.pack(">I", GETPLAYER_SLOTS) + bytes(8 * GETPLAYER_SLOTS)))})
    values.update(p.dlc)
    garage = cars.entries(p)
    if garage:
        values[72] = cars.garage_field(garage)
    else:
        values.pop(72, None)
    values.update({k: bytes.fromhex(v) for k, v in p.extra_fields.items()})
    values.update(p.unlocks)

    values[73] = cars.player_parts(garage, values[73], cars.array(len(p.owned_items), struct.pack(
        f">{len(p.owned_items)}Q", *p.owned_items)))
    packers = {"u8": ">B", "u16": ">H", "u32": ">I", "u64": ">Q"}
    out = b""
    for k, field_spec in enumerate(GETPLAYER_REPLY_FIELDS):
        kind = field_spec.partition(":")[0]
        value = values.get(k, 0)
        if kind in packers and isinstance(value, bytes):
            out += value
        elif kind in packers:
            out += struct.pack(packers[kind], value)
        elif kind == "f32":
            out += struct.pack(">I", value)
        elif kind == "u8x30":
            out += value[:30].ljust(30, b"\0") if isinstance(value, bytes) else bytes(30)
        elif kind == "guid":
            out += bytes(16)
        elif kind == "str":
            out += blob(value or b"")
        elif kind == "array":
            out += value or empty_array()
        elif kind in ("u8", "u16", "u32", "u64") and isinstance(value, bytes):
            out += value
    return 0x37E9E238, out
