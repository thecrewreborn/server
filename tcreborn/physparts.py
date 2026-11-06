# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import random
import struct

from tcreborn import cars, paths

PATH = paths.table("physparts.json")
BRONZE, SILVER, GOLD, PLATINUM, CAU_PLATINUM = range(5)
NO_BONUS, ALL_BONUSES = -1, 4


def load(path: str = PATH) -> dict | None:
    if not os.path.exists(path):
        return None
    with open(path) as f:
        t = json.load(f)
    t["ids"] = [int(x, 16) for x in t["ids"]]
    return t


TABLE = load()


def find(t: dict, slot: int, quality: int, level: int, bonus: int = NO_BONUS) -> int | None:
    best = None
    for i, (s, q, lv, b) in enumerate(t["parts"]):
        if s == slot and q == quality and (b == bonus or quality in (BRONZE, CAU_PLATINUM)) and lv <= level:
            if best is None or lv > t["parts"][best][2]:
                best = i
    return best


def _owned_at(cfg: bytes) -> int:
    (first,) = struct.unpack_from(">I", cfg, 0x6F)
    return 0x6F + 4 + first


def owned(cfg: bytes) -> list[int]:
    o = _owned_at(cfg)
    (n,) = struct.unpack_from(">I", cfg, o + 16)
    words = struct.unpack_from(f">{n}Q", cfg, o + 20)
    return [64 * w + b for w, x in enumerate(words) for b in range(64) if x >> b & 1]


def own(cfg: bytes, part: int) -> bytes:
    o = _owned_at(cfg)
    (n,) = struct.unpack_from(">I", cfg, o + 16)
    if part >> 6 >= n:
        return cfg
    out = bytearray(cfg)
    at = o + 20 + 8 * (part >> 6)
    struct.pack_into(">Q", out, at, struct.unpack_from(">Q", out, at)[0] | 1 << (part & 63))
    return bytes(out)


def fit(cfg: bytes, part: int, slot: int) -> bytes:
    out = bytearray(cfg)
    struct.pack_into(">H", out, 0x23 + 2 * slot, part)
    return bytes(out)


def give(p, model: int, spec: int, parts: list[int], mount: bool = False) -> bool:
    garage = cars.entries(p)
    configs = next((cs for m, cs in garage if m == model), None)
    k = next((i for i, c in enumerate(configs or []) if cars.spec(c) == spec), None)
    if k is None:
        return False
    cfg = configs[k]
    for part in parts:
        cfg = own(cfg, part)
        if mount and TABLE is not None:
            cfg = fit(cfg, part, TABLE["parts"][part][0])
    configs[k] = cfg
    cars.store(p, garage)
    return True


REWARD_PART = "CAbility_RewardPart"


REWARD_PARTS = False


def has_ability(p, name: str) -> bool:
    from tcreborn import rewards
    t = rewards.TABLES
    bit = t.abilities.get(name) if t is not None else None
    return bit is not None and bit // 64 < len(p.abilities) and p.abilities[bit // 64] >> (bit % 64) & 1 == 1


def mission_part(p, mission: int, medal: int, model: int, spec: int, rng=random) -> int | None:
    t = TABLE
    m = t["missions"].get(f"{mission:#x}") if t is not None else None
    if not REWARD_PARTS or m is None or not 0 <= medal < 4 or not has_ability(p, REWARD_PART):
        return None
    quality = m["quality"][min(medal, 2)]
    if quality < 0:
        return None
    bonus = rng.choices(range(4), weights=m["bonus"])[0] if quality not in (BRONZE, CAU_PLATINUM) else NO_BONUS
    part = find(t, m["slot"], quality, m["level"], bonus)
    if part is None or not give(p, model, spec, [part]):
        return None
    return part
