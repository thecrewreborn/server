# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import datetime as dt
import json
import os
import random

from tcreborn import paths

PATH = paths.table("summit.json")
NONE = 0xFFFF
FEE = 1000
EVENT_BUCKS, EVENT_XP = 3000, 800
MONTHLY_BUCKS, MONTHLY_XP = 6000, 1200
RANK_POINTS = [1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3]
POINTS_UNIT, NEXT_THRESHOLD = 25, 325
AUTO_ENTRY_RANK = 2
NO_KIND = 3


EVENT_SLOTS = [5, 6, 8, 9, 10, 11]


def load(path: str = PATH) -> dict | None:
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return json.load(f)


DATA = load()


def current(now_us: int, data: dict | None = None) -> int:
    data = data if data is not None else DATA
    if not data:
        return NONE
    n = dt.datetime.fromtimestamp(now_us / 1e6, dt.timezone.utc)
    key, found = (n.month, n.day, n.hour), NONE
    for i, r in enumerate(data["records"]):
        (sd, sm, sy, sh), (ed, em, _ey, eh) = r["start"], r["end"]
        a, b = (sm, sd, sh), (em, ed, eh)
        if n.year >= sy and (a <= key <= b if a <= b else (key >= a or key <= b)):
            found = i
    return found


def calendar_time_us(now_us: int, data: dict | None = None) -> int:
    data = data if data is not None else DATA
    if not data:
        return now_us
    end = max(dt.datetime(e[2], e[1], e[0], e[3], tzinfo=dt.timezone.utc) for e in (r["end"] for r in data["records"]))
    n = dt.datetime.fromtimestamp(now_us / 1e6, dt.timezone.utc)
    if n < end:
        return now_us
    year = end.year if (n.month, n.day, n.hour, n.minute) < (end.month, end.day, end.hour, end.minute) else end.year - 1
    day = min(n.day, 28) if n.month == 2 and n.day == 29 else n.day
    return now_us - int((n - n.replace(year=year, day=day)).total_seconds()) * 1_000_000


def state(p) -> dict:
    s = p.summit
    s.setdefault("entered", False)
    s.setdefault("index", NONE)
    s.setdefault("threshold", 1)
    s.setdefault("meter", 0)
    s.setdefault("last_kind", 0)
    s.setdefault("tokens", [0, 0, 0])
    s.setdefault("next_kind", NO_KIND)
    s.setdefault("rank", 0)
    s.setdefault("results", 0)
    s.setdefault("value", 0)
    s.setdefault("scores", {})
    return s


def sync(p, cur: int) -> None:
    s = state(p)
    if s["index"] != cur or cur == NONE:
        s["entered"], s["index"], s["scores"] = False, NONE, {}
    elif not s["entered"]:
        s["entered"] = True


def enter(p, cur: int) -> bool:
    s = state(p)
    if s["entered"] and s["index"] == cur:
        return True
    if p.bucks < FEE or cur == NONE:
        return False
    p.bucks -= FEE
    s["entered"], s["index"] = True, cur
    return True


def event_reward(cur: int, data: dict | None = None) -> tuple[int, int]:
    data = data if data is not None else DATA
    monthly = data["records"][cur].get("monthly", False) if data and cur != NONE else False
    return (MONTHLY_XP, MONTHLY_BUCKS) if monthly else (EVENT_XP, EVENT_BUCKS)


def event_slot(mission: int, cur: int, data: dict | None = None) -> int | None:
    data = data if data is not None else DATA
    if not data or cur == NONE:
        return None
    events = data["records"][cur]["events"]
    name = f"0x{mission:016x}"
    return EVENT_SLOTS[events.index(name)] if name in events else None


def post_score(p, slot: int, score: int) -> None:
    scores = state(p)["scores"]
    if score > scores.get(str(slot), 0):
        scores[str(slot)] = score


def add_points(p, rank: int) -> None:
    s = state(p)
    gain = RANK_POINTS[min(rank, len(RANK_POINTS) - 1)]
    total = gain + s["meter"]
    if total < s["threshold"]:
        s["meter"] = total
        return
    s["meter"] = 0
    s["threshold"] = NEXT_THRESHOLD // POINTS_UNIT // 10
    kind = s["next_kind"]
    s["last_kind"] = kind
    pick = random.randrange(3)
    if pick == kind:
        pick = pick + 1 if pick < 2 else 0
    s["next_kind"] = pick
    if kind in (0, 1, 2):
        s["tokens"][(1, 2, 0)[kind]] += 1


def result(p, rank: int, cur: int) -> None:
    s = state(p)
    s["rank"] = rank
    add_points(p, rank)
    if not s["entered"] and cur != NONE and rank + 1 >= AUTO_ENTRY_RANK + 1:
        monthly = DATA["records"][cur].get("monthly", False) if DATA else False
        if not monthly:
            s["entered"], s["index"] = True, cur
