# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import struct

from tcreborn.games.v1_21 import cars as cars121
from tcreborn.wire import empty_array

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "cars.json")) as _f:
    _DATA = json.load(_f)
PARTS = _DATA["parts"]
PERF_PARTS = _DATA["perf_parts"]
STOCK = {int(k, 16): v for k, v in _DATA["slots"].items()}

SLOTS = 25
KEPT = range(11, 25)
LIST_SLOTS = [i for i in cars121.LIST_SLOTS if i in KEPT]
EMPTY = 0xFFFF
OWNED_WORDS = 140
PLAYER_PARTS = 120


def slots_from_121(s121: list[int], variant: int | None = None) -> list[int]:
    kept = list(s121[11:25])
    stock121 = cars121.CATALOG.variants.get(variant, {}).get("slots", [])[11:25] if cars121.CATALOG else []
    if variant in STOCK:
        kept = [s1 if x == s0 else x for x, s0, s1 in zip(kept, list(stock121) + [None] * 14, STOCK[variant])]
    out = [x if x < PERF_PARTS else EMPTY for x in s121[:11]] + [EMPTY] * (SLOTS - 11)
    for k, x in enumerate(kept):
        out[11 + k] = x if x < PARTS else EMPTY
    return out


def owned_from_121(cfg: bytes) -> list[int]:
    o = 35 + 76
    o += 4 + struct.unpack_from(">I", cfg, o)[0]
    n = struct.unpack_from(">I", cfg, o + 16)[0]
    words = list(struct.unpack_from(f">{n}Q", cfg, o + 20))[:OWNED_WORDS]
    words += [0] * (OWNED_WORDS - len(words))
    for k in range(OWNED_WORDS):
        lo = 64 * k
        if lo + 64 > PERF_PARTS:
            words[k] &= (1 << max(0, PERF_PARTS - lo)) - 1
    return words


def config_from_121(cfg: bytes) -> bytes:
    head = cfg[:35]
    variant = struct.unpack_from(">Q", cfg, 8)[0]
    s121 = list(struct.unpack_from(">38H", cfg, 35))
    return (head + struct.pack(f">{SLOTS}H", *slots_from_121(s121, variant))
            + cars121.array(SLOTS, struct.pack(f">{SLOTS}I", *[0xFFFFFFFF] * SLOTS))
            + cars121.array(OWNED_WORDS, struct.pack(f">{OWNED_WORDS}Q", *owned_from_121(cfg))))


def part_list(s: list[int]) -> bytes:
    shown = [s[i] for i in LIST_SLOTS if s[i] != EMPTY]
    return cars121.array(len(shown), struct.pack(f">{len(shown)}H", *shown))


def garage_field(entries: list[tuple[int, list[bytes]]]) -> bytes:
    return cars121.garage_field([(m, [config_from_121(c) for c in cs]) for m, cs in entries])


def parts_field(entries: list[tuple[int, list[bytes]]], owned: list[int] = ()) -> bytes:
    words = [0] * PLAYER_PARTS
    for k, w in enumerate(list(owned)[:PLAYER_PARTS]):
        words[k] = w
    for _m, cs in entries:
        for cfg in cs:
            s = slots_from_121(list(struct.unpack_from(">38H", cfg, 35)), struct.unpack_from(">Q", cfg, 8)[0])
            for x in (s[i] for i in KEPT):
                if x != EMPTY:
                    words[x >> 6] |= 1 << (x & 63)
    for k in range(PLAYER_PARTS):
        lo = 64 * k
        if lo + 64 > PARTS:
            words[k] &= (1 << max(0, PARTS - lo)) - 1
    if not any(words):
        return empty_array()
    return cars121.array(PLAYER_PARTS, struct.pack(f">{PLAYER_PARTS}Q", *words))


def car_reply_from_121(car: bytes) -> bytes:
    model = struct.unpack_from(">q", car, 0)[0]
    n = struct.unpack_from(">I", car, 8)[0]
    configs_frame = car[12:12 + n]
    count = struct.unpack_from(">I", configs_frame, 12)[0]
    size = (len(configs_frame) - 16) // count if count else 0
    configs = [configs_frame[16 + k * size:16 + (k + 1) * size] for k in range(count)]
    if model == -1 or not configs:
        return struct.pack(">q", -1) + 2 * empty_array()
    last = configs[-1]
    return (struct.pack(">q", model) + cars121.array(len(configs), b"".join(config_from_121(c) for c in configs))
            + part_list(slots_from_121(list(struct.unpack_from(">38H", last, 35)),
                                       struct.unpack_from(">Q", last, 8)[0])))
