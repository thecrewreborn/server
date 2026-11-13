# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
from dataclasses import dataclass, field

from tcreborn.files import write_atomic
from tcreborn.games.v1_21 import tables

PERKS = 30


PERK_MAX = 10
STAT_COUNT = 438
RECORD_FIELDS = 15

MAX_RECORDS = 512
U8, U16, U32 = 0xFF, 0xFFFF, 0xFFFFFFFF
NO_ID = 0xFFFFFFFFFFFFFFFF

PROLOGUE_DONE = 0x20000000078439C6
STATE_KEYS = ("zone", "ids", "position", "direction", "abilities", "progress", "bucks", "map_state",
              "skill_last", "extra_fields", "missions_done", "activities_done", "level", "xp", "crew_credits", "perk_points", "field12", "garage", "garage_all", "free_roam",
              "perks", "rewarded", "table_bits", "owned_items", "summit", "group", "group_joined", "daily_last",
              "save_ids", "update_charge", "chase", "heat", "collectibles")
ALL_ABILITIES = [0xFFFFFFFFFFFFFFFF, 0x0FFBA3]
FOG_REVEALED = b"\xff" * 0x690


@dataclass
class Player:
    path: str | None = None
    stats: list[int] = field(default_factory=lambda: [0] * STAT_COUNT)
    records: dict[tuple[int, int], list[int]] = field(default_factory=dict)
    zone: int = 0
    ids: list[int] = field(default_factory=lambda: [NO_ID, NO_ID])
    position: list[int] = field(default_factory=lambda: [0, 0, 0])
    direction: list[int] = field(default_factory=lambda: [0, 0, 0])
    save_blob: bytes = b""
    abilities: list[int] = field(default_factory=list)
    unlocks: dict[int, bytes] = field(default_factory=dict)

    dlc: dict[int, bytes] = field(default_factory=lambda: dlc_fields("all"))

    progress: list[int] = field(default_factory=list)
    bucks: int = 0

    level: int = 1
    xp: int = 0
    crew_credits: int = 0
    cc_fixed: int = 0
    perk_points: int = 0
    perks: list[int] = field(default_factory=lambda: [0] * PERKS)
    field12: int = 0
    skill_last: int = 0


    extra_fields: dict[int, str] = field(default_factory=dict)


    missions_done: list[int] = field(default_factory=list)


    activities_done: list[int] = field(default_factory=list)

    garage: list[dict] = field(default_factory=list)
    garage_all: bool = False
    free_roam: bool = False
    rewarded: list[int] = field(default_factory=list)
    table_bits: list[int] = field(default_factory=list)
    owned_items: list[int] = field(default_factory=list)
    summit: dict = field(default_factory=dict)
    group: int = 0
    group_joined: int = 0
    daily_last: int = 0
    save_ids: list[int] = field(default_factory=lambda: [NO_ID] * 6)
    update_charge: int = 0
    chase: list[int] = field(default_factory=lambda: [NO_ID, 0xFF])
    heat: float = 0.0
    collectibles: list[list[int]] = field(default_factory=list)

    def unlock_free_roam(self) -> None:
        self.free_roam = True
        self.abilities = list(ALL_ABILITIES)
        self.save_blob = FOG_REVEALED

    @property
    def prologue_done(self) -> bool:
        return PROLOGUE_DONE in self.missions_done

    def complete_mission(self, mission: int) -> None:
        if mission not in self.missions_done:
            self.missions_done.append(mission)

    @property
    def cc(self) -> int:
        return _fit(self.cc_fixed or self.crew_credits, U32)

    def clamp(self) -> None:
        from tcreborn.games.v1_21 import rewards
        t = rewards.TABLES
        self.bucks = _fit(self.bucks, min(t.max_bucks, U32) if t is not None else U32)
        self.crew_credits = _fit(self.crew_credits, U32)
        self.xp = _fit(self.xp, U32)
        self.level = _fit(self.level, U8)
        self.perk_points = _fit(self.perk_points, U16)
        self.field12 = _fit(self.field12, U32)
        if any(not 0 <= x <= PERK_MAX for x in self.perks):
            self.perks = [_fit(x, PERK_MAX) for x in self.perks]
        s = self.summit
        if "tokens" in s and any(not 0 <= x <= U8 for x in s["tokens"]):
            s["tokens"] = [_fit(x, U8) for x in s["tokens"]]
        for k in ("threshold", "meter", "last_kind", "next_kind"):
            if k in s:
                s[k] = _fit(s[k], U8)

    def spend_cc(self, amount: int) -> bool:
        if self.cc < amount:
            return False
        if not self.cc_fixed:
            self.crew_credits -= amount
        return True

    def complete_activity(self, activity: int) -> None:
        if activity not in self.activities_done:
            self.activities_done.append(activity)


    map_state: list[int] = field(default_factory=list)

    earned: list = field(default_factory=list, repr=False, compare=False)

    @classmethod
    def load(cls, path: str | None) -> "Player":
        p = cls(path=path)
        if path and os.path.exists(path):
            with open(path) as f:
                data = json.load(f)
            p.stats = data["stats"][:STAT_COUNT] + [0] * max(0, STAT_COUNT - len(data["stats"]))
            p.records = {(r["a"], r["b"]): r["fields"] for r in data["records"]}
            for k in STATE_KEYS:
                if k in data:
                    setattr(p, k, data[k])
            if "level2" in data and "perk_points" not in data:
                p.perk_points = data["level2"]
            p.save_blob = bytes.fromhex(data.get("save_blob", ""))
            for k, v in data.items():
                if k.endswith("_fields") and k != "extra_fields" and isinstance(v, dict):
                    for f, x in v.items():
                        p.extra_fields.setdefault(f, x)
            p.extra_fields = {int(k): v for k, v in p.extra_fields.items()}
            p.clamp()
        return p

    def save(self) -> None:
        if not self.path:
            return
        self.clamp()
        os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)

        write_atomic(self.path, json.dumps({
            "stats": self.stats, "records": [{"a": a, "b": b, "fields": v} for (a, b), v in self.records.items()],
            **{k: getattr(self, k) for k in STATE_KEYS}, "save_blob": self.save_blob.hex()}).encode())

    def set_progress_bit(self, id_: int) -> None:
        set_bit(self.progress, id_)

    def set_stat(self, stat_id: int, value: int) -> None:

        if stat_id >= STAT_COUNT:
            return
        if stat_id >= len(self.stats):
            self.stats.extend([0] * (stat_id + 1 - len(self.stats)))
        self.stats[stat_id] = value

    def set_record_field(self, a: int, b: int, index: int, value: int) -> None:
        if (a, b) not in self.records and len(self.records) >= MAX_RECORDS:
            return
        rec = self.records.setdefault((a, b), [0] * RECORD_FIELDS)
        if index < RECORD_FIELDS:
            rec[index] = value


def _fit(x: int, hi: int) -> int:
    return min(max(int(x), 0), hi)


def set_bit(words: list[int], n: int) -> bool:
    word, bit = divmod(n, 64)
    if word >= len(words):
        words.extend([0] * (word + 1 - len(words)))
    if words[word] >> bit & 1:
        return False
    words[word] |= 1 << bit
    return True


def dlc_fields(owned: str) -> dict[int, bytes]:
    if owned.strip() == "none":
        return {}
    with open(tables.path("dlc.json")) as f:
        return {int(k): bytes.fromhex(v) for k, v in json.load(f)["fields"].items()}


def unlock_fields(names: str) -> dict[int, bytes]:
    wanted = {n.strip() for n in names.split(",") if n.strip() and n.strip() != "none"}
    return {}
