# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import random
import struct
from dataclasses import dataclass, field

PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "rewards.json")
TOP_LEVEL, MAX_BUCKS, MAX_LEVEL = 0, 1, 3
LEVEL_UP_PERK = 7
LEVEL_UP_CHANCE = [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20]
XP_MEDAL, BUCKS_MEDAL, BUCKS_BASE, XP_BASE, XP_NEXT = 0x07, 0x0B, 0x19, 0x55, 0xCD
ACTIVITY_XP_MEDAL, ACTIVITY_BUCKS_MEDAL = 0x0F, 0x13


def _f32(x: float) -> float:
    return struct.unpack("<f", struct.pack("<f", x))[0]


@dataclass
class RewardTables:
    table: list[int]
    reward_values: dict[int, list[int]]
    missions: dict[int, dict]
    abilities: dict[str, int]
    hidden: list[str] = field(default_factory=list)
    activities: dict[int, dict] = field(default_factory=dict)
    regions: dict[int, dict] = field(default_factory=dict)
    families: dict[int, list[float]] = field(default_factory=dict)

    @classmethod
    def from_json(cls, d: dict) -> "RewardTables":
        return cls(table=d["table"], reward_values={int(k, 16): v for k, v in d["reward_values"].items()},
                   missions={int(k, 16): v for k, v in d["missions"].items()}, abilities=d["abilities"],
                   hidden=d.get("hidden", []),
                   activities={int(k, 16): v for k, v in d.get("activities", {}).items()},
                   regions={int(k, 16): v for k, v in d.get("regions", {}).items()},
                   families={int(k, 16): v for k, v in d.get("families", {}).items()})

    def f(self, i: int) -> float:
        return struct.unpack("<f", struct.pack("<I", self.table[i]))[0]

    def factors(self, rv: int) -> list[float]:
        return [struct.unpack("<f", struct.pack("<I", w))[0] for w in self.reward_values[rv]]

    @property
    def max_level(self) -> int:
        return self.table[MAX_LEVEL]

    @property
    def max_bucks(self) -> int:
        return self.table[MAX_BUCKS]


def load(path: str = PATH) -> RewardTables | None:
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return RewardTables.from_json(json.load(f))


TABLES = load()


def _medal(t: RewardTables, thresholds: list[int], score: int, level: int) -> int:
    m = next((i for i in (3, 2, 1, 0) if thresholds[i] <= score), 0)
    return 2 if m == 3 and level < t.max_level else m


def mission_reward(t: RewardTables, mission: int, score: int, level: int) -> tuple[int, int]:
    m = t.missions.get(mission)
    if m is None or not m["rewarding"]:
        return 0, 0
    kind = m["type"]
    if kind in (0, 1, 5, 6) and level < m["min_level"]:
        return 0, 0
    xp_factor, bucks_factor = t.factors(int(m["reward_values"], 16))[:2]
    if kind in (0, 1, 6):
        base = level if kind == 6 else m["max_level"]
        medal = _medal(t, m["thresholds"], score, level)
        bucks = _f32(_f32(bucks_factor * t.f(BUCKS_BASE + base - 1)) * t.f(BUCKS_MEDAL + medal))
        xp = _f32(_f32(xp_factor * t.f(XP_BASE + base - 1)) * t.f(XP_MEDAL + medal))
    elif kind == 2:
        base = level
        bucks = _f32(bucks_factor * t.f(BUCKS_BASE + base - 1))
        xp = _f32(xp_factor * t.f(XP_BASE + base - 1))
    else:
        return 0, 0
    bucks, xp = _f32(bucks * m["factor"]), _f32(xp * m["factor"])
    if kind in (0, 1) and level - base > 10:
        fade = min(max(_f32(_f32(level - base - 10.0) / 10.0), 0.0), 1.0)
        xp = _f32(xp * _f32(1.0 - fade))
    return int(xp), int(bucks)


def activity_reward(t: RewardTables, activity: int, value: int, level: int) -> tuple[int, int]:
    a = t.activities.get(activity)
    if a is None:
        return 0, 0
    region, family = t.regions.get(int(a["region"], 16)), t.families.get(int(a["family"], 16))
    if region is None or family is None:
        return 0, 0
    th = a["thresholds"]
    medal = next((i for i in (3, 2, 1, 0) if th[i] <= value), None)
    if medal is None:
        return 0, 0
    if medal == 3 and level < t.max_level:
        medal = 2
    fade = 1.0 if level <= region["level"] or level - region["level"] < 20 else 0.0
    bucks = _f32(_f32(region["bucks"] * family[1]) * t.f(ACTIVITY_BUCKS_MEDAL + medal))
    xp = _f32(_f32(_f32(region["xp"] * family[0]) * fade) * t.f(ACTIVITY_XP_MEDAL + medal))
    return int(xp), int(bucks)


def abilities(t: RewardTables, missions: list[int]) -> list[int]:
    bits = set()
    for mission in missions:
        for name in t.missions.get(mission, {}).get("abilities", []):
            if name in t.abilities and name not in t.hidden:
                bits.add(t.abilities[name])
    if not bits:
        return []
    words = [0] * (max(bits) // 64 + 1)
    for b in bits:
        words[b // 64] |= 1 << (b % 64)
    return words


def add_bucks(t: RewardTables, player, d: int) -> None:
    if d < 0:
        player.bucks = max(0, player.bucks + d)
    elif d > 0:
        player.bucks = min(t.max_bucks, player.bucks + d)


def _xp_next(t: RewardTables, level: int) -> int:
    return t.table[XP_NEXT + level - 1] if 0 <= level - 1 < 60 else 0xFFFFFFFF


def level_up(t: RewardTables, player) -> None:
    while _xp_next(t, player.level) < player.xp:
        player.xp -= _xp_next(t, player.level)
        if player.level < t.table[TOP_LEVEL]:
            player.level += 1
        chance = LEVEL_UP_CHANCE[min(player.perks[LEVEL_UP_PERK], len(LEVEL_UP_CHANCE) - 1)]
        player.perk_points += 1 + (random.randrange(100) < chance)


def add_xp(t: RewardTables, player, d: int) -> None:
    if d == 0 or player.level >= t.table[TOP_LEVEL]:
        return
    player.xp += d
    level_up(t, player)
    player.xp = min(player.xp, _xp_next(t, t.table[TOP_LEVEL]))


def apply(t: RewardTables, player, xp: int, bucks: int) -> None:
    add_bucks(t, player, bucks)
    add_xp(t, player, xp)
