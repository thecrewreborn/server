# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import struct
from dataclasses import dataclass

from tcreborn.games.v1_21 import tables
from tcreborn.wire import T_ARRAY, blob, frame

PATH = tables.path("cars.json")
NONE = 0xFFFFFFFFFFFFFFFF
OWNED_WORDS = 0x8C
OWNED_SLOTS = 11
LIST_SLOTS = list(range(11, 19)) + list(range(28, 38)) + [19, 20, 21] + list(range(23, 28))
NEW, STORED = 4, 1
BOUGHT = 3
CC_RATE = 4.50045

EMPTY_CONFIG = (bytes(0x6F) + blob(frame(T_ARRAY, bytes(4))) + struct.pack(">5I", 0x10, 0x10, T_ARRAY, 0x21000000, 0)
                + bytes([BOUGHT]))


@dataclass
class Catalog:
    variants: dict[int, dict]
    parts: dict[int, int]
    prices: dict[int, int]
    budget: int
    dealer: int
    slot_defaults: list[int]
    spec_prices: dict[int, list[int]] = None
    special_specs: list[int] = None

    @classmethod
    def from_json(cls, d: dict) -> "Catalog":
        return cls(variants={int(k, 16): v for k, v in d["variants"].items()},
                   parts={int(k, 16): v for k, v in d["parts"].items()},
                   prices={int(k, 16): v for k, v in d["prices"].items()},
                   budget=d["budget"], dealer=int(d["dealer"], 16), slot_defaults=d["slot_defaults"],
                   spec_prices={int(k, 16): v for k, v in d.get("spec_prices", {}).items()},
                   special_specs=[int(k, 16) for k in d.get("special_specs", [])])

    def part(self, pid: int) -> int:
        return self.parts.get(pid, 0xFFFF)


def load(path: str = PATH) -> Catalog | None:
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return Catalog.from_json(json.load(f))


CATALOG = load()


def array(count: int, items: bytes) -> bytes:
    return blob(frame(T_ARRAY, struct.pack(">I", count) + items))


def slots(c: Catalog, variant: int, a: int, b: int, d: int) -> list[int]:
    v = c.variants[variant]
    s = list(v["slots"])
    if d != NONE:
        s[20] = c.part(d)
    if a != NONE:
        s[21] = c.part(a)
    s[22] = s[21]
    s[24] = c.part(int(v["default_part"], 16) if b == NONE else b)
    return s


def config(c: Catalog, variant: int, s: list[int], state: int) -> bytes:
    owned = [0] * OWNED_WORDS
    for x in s[:OWNED_SLOTS]:
        if x != 0xFFFF:
            owned[x >> 6] |= 1 << (x & 63)
    return (struct.pack(">QQBB", int(c.variants[variant]["spec"], 16), variant, 1, 0x64) + bytes(17)
            + struct.pack(">38H", *s)
            + array(len(c.slot_defaults), struct.pack(f">{len(c.slot_defaults)}I", *c.slot_defaults))
            + array(OWNED_WORDS, struct.pack(f">{OWNED_WORDS}Q", *owned)) + bytes([state]))


def part_list(s: list[int]) -> bytes:
    shown = [s[i] for i in LIST_SLOTS if s[i] != 0xFFFF]
    return array(len(shown), struct.pack(f">{len(shown)}H", *shown))


def garage_field(entries: list[tuple[int, list[bytes]]]) -> bytes:
    return array(len(entries), b"".join(struct.pack(">Q", m) + array(len(cs), b"".join(cs)) for m, cs in entries))


def parse_garage(field: bytes) -> list[tuple[int, list[bytes]]]:
    out, (n,) = [], struct.unpack_from(">I", field, 16)
    o = 20
    for _ in range(n):
        model, size = struct.unpack_from(">QI", field, o)
        inner = field[o + 12:o + 12 + size]
        (count,) = struct.unpack_from(">I", inner, 12)
        out.append((model, _split_configs(inner[16:], count)))
        o += 12 + size
    return out


def _split_configs(raw: bytes, count: int) -> list[bytes]:
    out, o = [], 0
    for _ in range(count):
        start = o
        o += 0x6F
        for _arr in range(2):
            o += 4 + struct.unpack_from(">I", raw, o)[0]
        o += 1
        out.append(raw[start:o])
    return out


def spec(cfg: bytes) -> int:
    return struct.unpack_from(">Q", cfg, 0)[0]


def variant_of(cfg: bytes) -> int:
    return struct.unpack_from(">Q", cfg, 8)[0]


def with_state(cfg: bytes, state: int) -> bytes:
    return cfg[:-1] + bytes([state])


PLAYER_PARTS = 250


def player_parts(entries: list[tuple[int, list[bytes]]], *fields: bytes) -> bytes:
    words = [0] * PLAYER_PARTS
    for f in fields:
        n = struct.unpack_from(">I", f, 16)[0]
        for i, w in enumerate(struct.unpack_from(f">{n}Q", f, 20)[:PLAYER_PARTS]):
            words[i] |= w
    for _model, configs in entries:
        for cfg in configs:
            if len(cfg) < 0x23 + 76:
                continue
            for x in struct.unpack_from(">38H", cfg, 0x23)[OWNED_SLOTS:]:
                if x != 0xFFFF and x >> 6 < PLAYER_PARTS:
                    words[x >> 6] |= 1 << (x & 63)
    return array(PLAYER_PARTS, struct.pack(f">{PLAYER_PARTS}Q", *words))


def entries(p) -> list[tuple[int, list[bytes]]]:
    if 72 in p.extra_fields and not p.garage_all:
        return parse_garage(bytes.fromhex(p.extra_fields[72]))
    own = [(g["model"], [bytes.fromhex(c) for c in g["configs"]]) for g in p.garage]
    if 72 not in p.dlc or not p.prologue_done:
        return own
    models = {m for m, _ in own}
    return [e for e in parse_garage(p.dlc[72]) if e[0] not in models] + own


def store(p, garage: list[tuple[int, list[bytes]]]) -> None:
    p.garage = [{"model": m, "configs": [c.hex() for c in cs]} for m, cs in garage]
    p.garage_all = True
    p.extra_fields.pop(72, None)


def spec_price(c: Catalog, garage: list[tuple[int, list[bytes]]], variant: int) -> int | None:
    v = c.variants[variant]
    sp = int(v["spec"], 16)
    owned = {spec(cfg) for _m, cs in garage for cfg in cs}
    if sp in c.special_specs:
        charged = bool(owned & set(c.special_specs))
    else:
        charged = sp in owned
    prices = c.spec_prices.get(sp, [])
    if not charged or not 0 < v["tier"] <= len(prices):
        return None
    return prices[v["tier"] - 1]
