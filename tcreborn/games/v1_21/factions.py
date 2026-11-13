# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import random
import threading

from tcreborn import config, paths


PATH = paths.home("save", "factions.json") if config.save_path_configured() else paths.home("save", "1.21",
                                                                                             "factions.json")
HOUR = 3600
MAX_CATCH_UP = 24 * 14
FIELDS = list(range(0x50, 0xA0, 4))

START = [
    [22940, 5158, 9831, 15281, 19243, 22940, 6000, 12846, 1, 6193, 50, 2523, 815, 802, 2037, 206, 4075, 0, 8150, 0],
    [7729, 1315, 2576, 4451, 5757, 7729, 64912, 4328, 1, 2086, 30, 850, 304, 270, 760, 69, 1521, 0, 3042, 0],
    [14465, 3021, 5834, 8622, 10857, 14465, 44740, 8100, 1, 3905, 47, 1591, 479, 506, 1197, 130, 2395, 0, 4791, 0],
    [9269, 1679, 3480, 5448, 7424, 9269, 61728, 5190, 1, 2502, 33, 1019, 331, 324, 829, 83, 1659, 0, 3318, 0],
    [16507, 3596, 7107, 9321, 13078, 16507, 29968, 9243, 1, 4456, 50, 1815, 607, 577, 1517, 148, 3035, 0, 6071, 0],
]


def _i(off: int) -> int:
    return FIELDS.index(off)


def tick(rec: list[int], rng: random.Random) -> None:
    total = 0
    for k, off in enumerate((0x54, 0x58, 0x5C, 0x60, 0x64)):
        total += rng.randint(6, 19)
        rec[_i(off)] += total
    rec[_i(0x50)] = rec[_i(0x64)]
    for off, share in ((0x6C, 0.56), (0x74, 0.27), (0x7C, 0.11)):
        rec[_i(off)] += int(total * share + rng.random())
    rec[_i(0x84)] += rng.randint(1, 3)
    rec[_i(0x8C)] += rng.random() < 0.5
    rec[_i(0x68)] = rng.randint(6000, 65000)
    rec[_i(0x78)] = rng.randint(25, 50)
    rec[_i(0x80)] = int(rec[_i(0x84)] * rng.uniform(0.55, 1.05))
    rec[_i(0x88)] = int(rec[_i(0x80)] * 2.5)
    rec[_i(0x90)] = rec[_i(0x88)] * 2
    rec[_i(0x98)] = rec[_i(0x90)] * 2


def load(now: int, path: str | None = None) -> list[list[int]]:
    with _LOCK:
        return _load(now, path or PATH)


_LOCK = threading.Lock()


def _load(now: int, path: str) -> list[list[int]]:
    try:
        with open(path) as f:
            state = json.load(f)
    except (OSError, ValueError):
        state = {"time": now, "groups": [list(r) for r in START]}
    hours = min(max(0, (now - state["time"]) // HOUR), MAX_CATCH_UP)
    if hours:
        rng = random.Random()
        for _ in range(hours):
            for rec in state["groups"]:
                tick(rec, rng)
        state["time"] = state["time"] + hours * HOUR if hours < MAX_CATCH_UP else now
    if hours or not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(state, f)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    return state["groups"]


def field(rec: list[int], off: int) -> int:
    return rec[_i(off)]
