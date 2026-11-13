# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import struct

from tcreborn.games.v1_21 import handlers as base
from tcreborn.games.v1_21.handlers import login
from tcreborn.games.v1_00 import translate
from tcreborn.games.v1_00 import cars
from tcreborn.games.v1_21 import cars as cars121
from tcreborn.games.v1_21 import physparts
from tcreborn.games.v1_21.handlers.mission import (dealer_car, finish_mission, is_dealer, owned_dealer_car,
                                                   pays_dealer, reward_part)
from tcreborn.wire import blob, empty_array, frame, serializer_list, u64_array

HANDLERS = {}
NAMES = {}


def register(req_id: int, name: str):
    def deco(fn):
        HANDLERS[req_id] = fn
        NAMES[req_id] = name
        return fn
    return deco


_DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
with open(os.path.join(_DATA, "replies.json")) as _f:
    _REPLIES = {k: v for k, v in json.load(_f).items() if not k.startswith("_")}
AUTO = {int(k, 16): (int(v["reply_id"], 16), v["layout"], v["request"].split("(")[0]) for k, v in _REPLIES.items()}

NONE_FIELDS = {int(k, 16): v["none"] for k, v in _REPLIES.items() if v.get("none")}

NONE_FIELDS.pop(0x0BE1ACD4, None)


def auto_payload(req_id: int) -> bytes:
    _reply_id, layout, _ = AUTO[req_id]
    tokens = translate.expand(layout)
    none = set(NONE_FIELDS.get(req_id, ()))
    return translate.encode(tokens, [-1 if k in none else None for k in range(len(tokens))])
with open(os.path.join(os.path.dirname(os.path.abspath(base.__file__)), "..", "schema", "replies.json")) as _f:
    SCHEMA_121 = {int(k, 16): v["layout"] for k, v in json.load(_f).items() if not k.startswith("_")}


ALIASES: dict[int, int] = {


    0x8711A3A7: 0x9FD2D3D4,


    0x3CDDE44F: 0x49466F25,
}

LAYOUTS_121: dict[int, list[str]] = {
    0x3069959F: ["u8", "u16"],


    0x3D3D4841: ["u64", "u8", "u64*2", "u32*2", "u32*5", "u32*12", "u64*25"],
}


def _pick(n: int, pairs: dict[int, int]) -> list[int | None]:
    return [pairs.get(k) for k in range(n)]


OVERRIDES: dict[int, list[int | None]] = {
    0x7932CE3E: _pick(19, {0: 0, 1: 1, 2: 2, 3: 3, 15: 4}),
    0x82B2A771: _pick(43, {0: 0, 1: 1, 2: 3, 14: 2, **{18 + k: 23 + k for k in range(25)}}),

    0xA7476B7D: _pick(62, {0: 0, 1: 1, 2: 2, 3: 51, 16: 20, 17: 21, 18: 23, 30: 22,
                           **{34 + k: 26 + k for k in range(25)}}),
}


def name_of(req_id: int) -> str:
    if req_id in NAMES:
        return NAMES[req_id]
    if req_id in AUTO:
        return AUTO[req_id][2]
    return base.name_of(req_id)


def answered_by_121(req_id: int) -> int | None:
    req121 = ALIASES.get(req_id, req_id)
    return req121 if req_id in AUTO and (req121 in base.HANDLERS or req121 in base.AUTO) else None


def layout_121(reply_id: int, req121: int) -> list[str] | None:
    if reply_id in SCHEMA_121:
        return SCHEMA_121[reply_id]
    if req121 in base.AUTO and base.AUTO[req121][0] == reply_id:
        return base.AUTO[req121][1]
    return LAYOUTS_121.get(reply_id)


def translated(ctx, req_id: int, req121: int, body: bytes):
    reply_id, layout, _ = AUTO[req_id]
    inner = base.handle(ctx, req121, body)
    if not inner:
        return inner
    rid121 = struct.unpack_from(">I", inner, 4)[0]
    if rid121 == reply_id:
        return inner
    layout121 = layout_121(rid121, req121)
    if layout121 is None:
        return frame(reply_id, auto_payload(req_id))
    try:
        payload = translate.transcode(inner[12:], layout121, layout, OVERRIDES.get(req_id))
    except struct.error as e:
        print(f"[1.00] {name_of(req_id)}: the 1.21 reply does not fit its layout ({e}); sent the default reply",
              flush=True)
        payload = auto_payload(req_id)
    return frame(reply_id, payload)


def handle(ctx, req_id: int, body: bytes):
    if req_id in HANDLERS:
        ctx.player.clamp()
        result = HANDLERS[req_id](ctx, body)
        return base.NO_REPLY if result is None else frame(*result)
    req121 = answered_by_121(req_id)
    if req121 is not None:
        return translated(ctx, req_id, req121, body)
    if req_id in AUTO:
        return frame(AUTO[req_id][0], auto_payload(req_id))
    return base.handle(ctx, req_id, body)


T_NET_VALUES = 0x56F3387D


NET_CONFIG = {"Punch": {"ALLOW_PLAY_WITHOUT_PUNCH": "1", "MAX_LOGIN_WAIT_TIME_MS": 5000}}
T_NET_VALUE_INT = 0x564E9254


def _net_config() -> dict:
    merged = {name: dict(values) for name, values in login.NET_CONFIG.items()}
    for name, values in NET_CONFIG.items():
        merged.setdefault(name, {}).update(values)
    return merged


def net_values() -> bytes:
    def value(key, v):
        if isinstance(v, int):
            return frame(T_NET_VALUE_INT, login._str(key) + struct.pack(">I", v))
        return frame(login.T_CONFIG_VALUE, login._str(key) + login._str(v))
    sections = [frame(login.T_CONFIG_SECTION, login._str(name) + blob(serializer_list(
                    [value(k, v) for k, v in values.items()])))
                for name, values in _net_config().items()]
    return frame(T_NET_VALUES, blob(serializer_list(sections)))


@register(0x2E0E24CB, "LOAD_ENTITY_READONLY")
def load_entity_read_only(ctx, body: bytes):
    return 0xCE8FE9D1, struct.pack(">BQ", 1, 0) + blob(net_values()) + struct.pack(">I", 0)


PLAYER_FIELDS = [
    ("u8:ok", 0), ("u64:player_id", 1), ("u64:player_b", 2), ("u64:player_c", 3), ("guid:profile_id", 4),
    ("str:name", 5), ("u8:level", 7), ("u32:xp", 8), ("u32:bucks", 9), ("u32:cc", 10),
    ("u32:field12", 12), ("u16:perk_points", 13), ("u64:field14", 14), ("u32:zone", 15), ("u64:id0", 16),
    ("u64:id1", 17), *((f"f32:position{k}", 18 + k) for k in range(6)),
    ("array", 24), ("array:missions_done", 25), ("array:abilities", 26), ("array:map_state", 27),
    ("array:rewarded", 28), ("array:table_bits", 29), ("array:progress", 30),
    ("u32", 31), ("u8", 32), ("u8x30:perks", 33), ("u32", 34), ("u32", 35), ("u32", 36), ("array", 37),
    ("u64:player_id_again", 71), ("array:garage", None), ("array:parts", None), ("u32", 75), ("u8:new_player", 77),
]
FIELD_SIZE = {"u8": 1, "u16": 2, "u32": 4, "u64": 8, "f32": 4, "u8x30": 30, "guid": 16}


def split_fields(specs: list[str], body: bytes) -> list[bytes]:
    out, pos = [], 0
    for spec in specs:
        kind = spec.partition(":")[0]
        n = FIELD_SIZE[kind] if kind in FIELD_SIZE else 4 + struct.unpack_from(">I", body, pos)[0]
        out.append(body[pos:pos + n])
        pos += n
    if pos != len(body):
        raise ValueError(f"reply has {len(body)} bytes, its fields {pos}")
    return out


ABILITY_BITS = range(1, 69)


def abilities(words: list[int]) -> list[int]:
    mask = [0, 0]
    for b in ABILITY_BITS:
        mask[b >> 6] |= 1 << (b & 63)
    out = [w & (mask[k] if k < len(mask) else 0) for k, w in enumerate(words[:len(mask)])]
    while out and not out[-1]:
        out.pop()
    return out


@register(0x5EAA9404, "GET_OR_CREATE_PLAYER")
def get_or_create_player(ctx, body: bytes):
    inner = base.handle(ctx, 0x589F57FB, body)
    fields = split_fields(login.GETPLAYER_REPLY_FIELDS, inner[12:])
    garage = cars121.entries(ctx.player)
    own = {"array:garage": cars.garage_field(garage) if garage else empty_array(), "array:parts": cars.parts_field(garage, ctx.player.owned_items),
           "array:abilities": u64_array(abilities(ctx.player.abilities)) if ctx.player.abilities else empty_array()}
    return 0xA555E30A, b"".join(own[spec] if spec in own else fields[k] for spec, k in PLAYER_FIELDS)


T_RULE_PROXY = 0xD4150057
T_RULE_MODULO = 0xEDAC67C4
PROXY_RULES = {"RecordsService"}


def _rules_with_proxy(inner: bytes, names: set) -> bytes:
    body = inner[12:]
    n = struct.unpack_from(">I", body, 0)[0]
    lst = body[4:4 + n]
    elems, o = [], 12
    while o < len(lst):
        m = struct.unpack_from(">I", lst, o)[0]
        el = lst[o + 4:o + 4 + m]
        o += 4 + m
        sl = struct.unpack_from(">I", el, 12)[0]
        name = el[16:16 + sl]
        if struct.unpack_from(">I", el, 4)[0] == T_RULE_MODULO and name.decode() in names:
            el = frame(T_RULE_PROXY, blob(name))
        elems.append(el)
    return frame(struct.unpack_from(">I", inner, 4)[0], blob(serializer_list(elems)) + body[4 + n:])


@register(0x27C711C5, "SCALABILITY_RULES")
def scalability_rules(ctx, body: bytes):
    inner = _rules_with_proxy(base.handle(ctx, 0x27C711C5, body), PROXY_RULES)
    return struct.unpack_from(">I", inner, 4)[0], inner[12:]


@register(0x33F6A6A1, "GET_PVP_SCORE")
def get_pvp_score(ctx, body: bytes):
    return 0x2B1D5CA7, b"\x01" + blob(serializer_list([]))


@register(0xB84353AD, "VALIDATE_MISSION")
def validate_mission(ctx, body: bytes):
    mission, score = struct.unpack_from(">QI", body, 0x1C)
    p = ctx.player
    dealer = pays_dealer(p, mission)
    finish_mission(p, mission, score, ctx.now_us())
    part = reward_part(p, mission, score, body)
    car = dealer_car(p, body) if dealer else owned_dealer_car(p, body) if is_dealer(mission) else None
    p.save()
    granted = u64_array([physparts.TABLE["ids"][part]]) if part is not None and physparts.ANNOUNCE_PART \
        and part < cars.PERF_PARTS else empty_array()
    return 0xAED45904, (struct.pack(">BQ5I", 1, mission, p.level, p.xp, p.bucks, p.cc, p.perk_points)
                        + granted + bytes(12 * 4) + u64_array(abilities(p.abilities))
                        + (cars.car_reply_from_121(car) if car else _no_car()) + struct.pack(">I", 0))


@register(0xC43C7F34, "PROGRESS_BIT")
def progress_bit(ctx, body: bytes):
    (id_,) = struct.unpack_from(">H", body, 0x1C)
    ctx.player.set_progress_bit(id_)
    ctx.player.save()
    return 0x51495E7F, b"\x01" + u64_array(ctx.player.progress) + struct.pack(">I", 0)


def _no_car() -> bytes:
    return struct.pack(">q", -1) + 2 * empty_array()


@register(0x88648CE9, "SECRET_CAR")
def retrieve_secret_car(ctx, body: bytes):
    inner = base.handle(ctx, 0x88648CE9, body)
    status, model = struct.unpack_from(">Hq", inner, 12)
    if model == -1:
        return 0x6939A37D, struct.pack(">H", 0) + _no_car()
    return 0x6939A37D, struct.pack(">H", status) + cars.car_reply_from_121(inner[14:])


@register(0xB11F578E, "BUY_NEW_CAR")
def buy_new_car(ctx, body: bytes):
    inner = base.handle(ctx, 0xBBF8C44B, body)
    status, bucks, cc, x = struct.unpack_from(">HIII", inner, 12)
    return 0xA0B9C4D7, (struct.pack(">IIII", status, bucks, cc, x)
                        + cars.car_reply_from_121(inner[26:]))


@register(0xB9BE3AE1, "USE_MONEY")
def use_money(ctx, body: bytes):
    p = ctx.player
    return AUTO[0xB9BE3AE1][0], struct.pack(">B6I", 1, 0, p.bucks, 0, 0, 0, 0)


def _meta_block(ctx) -> bytes:
    p = ctx.player
    return (struct.pack(">QBQ", ctx.player_id, p.group, p.group_joined if p.group else 0) + bytes(11 * 4)
            + struct.pack(">Q", ctx.now_us()) + empty_array() + struct.pack(">HI", 0, 0))


def _meta_result(ctx, reply_id: int, extra: int):
    p = ctx.player
    return reply_id, (b"\x01" + _meta_block(ctx) + struct.pack(">5I", p.level, p.xp, p.bucks, p.cc, p.perk_points)
                      + bytes(4 * extra))


@register(0x31414F5F, "FACTION_MISSION")
def validate_faction_mission(ctx, body: bytes):
    return _meta_result(ctx, 0x1D31095E, 0)


@register(0x60875720, "COLLECT_SEND_FRIEND_MISSION")
def collect_send_friend_mission(ctx, body: bytes):
    return _meta_result(ctx, 0xD9F5D2BF, 0)


@register(0xF99ADFB6, "PVP_MISSION")
def validate_pvp_mission(ctx, body: bytes):
    return _meta_result(ctx, 0xDC5C75CA, 1)


@register(0x0D0AE2E9, "SELL_ITEM")
def sell_item(ctx, body: bytes):
    return 0x71F77321, struct.pack(">II", 0, ctx.player.bucks)


@register(0xEA9859E1, "PERKS")
def save_crew_comforts(ctx, body: bytes):
    body = body.ljust(0x3D, b"\0")
    b121 = bytearray(0x3F)
    b121[:0x1C] = body[:0x1C]
    b121[0x1C] = body[0x1C]
    b121[0x21:0x3F] = body[0x1F:0x3D]
    inner = base.handle(ctx, 0xBF9B8FBD, bytes(b121))
    reply_id, layout, _ = AUTO[0xEA9859E1]
    return reply_id, translate.transcode(inner[12:], ["u8", "u32*2", "u16*2"], layout)


DELETE_BACKUPS = 5


@register(0x19B432B3, "DELETE_PLAYER")
def delete_player(ctx, body: bytes):
    import shutil
    import time
    p = ctx.player
    if p.path and os.path.exists(p.path):
        backup = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(p.path))), "backup")
        os.makedirs(backup, exist_ok=True)
        stem = os.path.splitext(os.path.basename(p.path))[0]
        shutil.copyfile(p.path, os.path.join(backup, f"{stem}-deleted-{time.strftime('%Y%m%d-%H%M%S')}.json"))
        own = sorted(f for f in os.listdir(backup) if f.startswith(f"{stem}-deleted-") and f.endswith(".json"))
        for old in own[:-DELETE_BACKUPS]:
            try:
                os.remove(os.path.join(backup, old))
            except OSError:
                pass
    keep = {k: getattr(p, k) for k in ("unlocks", "dlc", "free_roam")}
    fresh = type(p)(path=p.path)
    p.__dict__.update(fresh.__dict__)
    p.__dict__.update(keep)
    from tcreborn.games.v1_21.relay import start_crew_credits
    start_crew_credits(p, new=True)
    if p.free_roam:
        p.unlock_free_roam()
    p.save()
    return 0x3EC53E54, struct.pack(">BI", 1, 0)


@register(0xABBB99E1, "CREATE_PLAYER")
def create_player(ctx, body: bytes):
    _, inner = get_or_create_player(ctx, body)
    fields = split_fields([s for s, _ in PLAYER_FIELDS], inner)
    return 0x86D9FE30, fields[0] + struct.pack(">I", 0) + b"".join(fields[1:-2])


@register(0xF70D87D3, "SPEC_PURCHASE")
def buy_dress(ctx, body: bytes):
    try:
        currency = body[0x1C]
        (count,) = struct.unpack_from(">I", body, 0x1D + 16)
        model = struct.unpack_from(">Q", body, 0x1D + 20)[0] if count else cars121.NONE
        (n,) = struct.unpack_from(">I", body, 0x1D + 28 + 16) if count else (0,)
        ids = struct.unpack_from(">7Q", body, 0x1D + 28 + 20) if n else ()
    except struct.error:
        currency, count, model, n, ids = 0, 0, cars121.NONE, 0, ()
    inner_array = cars121.array(1, struct.pack(">10Q", *ids, *[cars121.NONE] * 3)) if n else cars121.array(0, b"")
    b121 = body[:0x1C] + bytes([currency]) + (cars121.array(1, struct.pack(">Q", model) + inner_array) if count
                                               else cars121.array(0, b""))
    inner = base.handle(ctx, 0xF70D87D3, b121)
    status, bucks, cc, x = struct.unpack_from(">HIII", inner, 12)
    (blen,) = struct.unpack_from(">I", inner, 26)
    element121 = inner[26 + 4 + 16:26 + 4 + blen]
    car = cars.car_reply_from_121(element121)
    return AUTO[0xF70D87D3][0], struct.pack(">IIII", status, bucks, cc, x) + cars121.array(1, car)
