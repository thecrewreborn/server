# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcb
import gamefiles
import gen_cars
import gen_rewards

DB = "road66database\\physcarpartdb.bin"
ITEM = 0x7CC1C83B
SLOT_ID, QUALITY_ID, ITEM_LEVEL, CAU_LEVEL = 0x3CC33762, 0x755C763A, 0x4CCF5554, 0xE106E9E4
BONUS = [0x4A908E44, 0xE3CE67A3, 0x3F3E0A6C, 0x56607A93]


SLOTS = [0x3756AA, 0x3756AB, 0x3756AC, 0x375FC2, 0x375FC5, 0x375FC7, 0x375FCF, 0x375FD1, 0x375FD2, 0x375FD3,
         0x375FD4]
QUALITIES = [0xB9FE99, 0xB9FE9A, 0xB9FE9B, 0xB9FE9C, 0x794A351]


def item(arch: gen_cars.Archetypes, e, field: int):
    for _ in range(32):
        if e is None:
            return None
        c = next((x for x in gen_cars.walk(e) if x.cls == ITEM), None)
        v = arch.raw(c, field) if c is not None else None
        if v is not None:
            return int.from_bytes(v, "little")
        f = arch.raw(e, gen_cars.FATHER)
        e = arch.by_id.get(int.from_bytes(f, "little")) if f else None
    return None


def crc(name: str) -> int:
    return zlib.crc32(name.encode())


def mission_parts(d: str) -> dict:
    R = gen_rewards
    data = R.load(d)
    db = {**data["db"], **data["spawners"]}
    out = {}
    for mid in data["spawners"]:
        e = R.resolve(mid, db)
        rd = e.kid(R.REWARD_DATA) if e else None
        stack, rv = [rd] if rd else [], None
        while stack:
            x = stack.pop()
            stack.extend(x.kids)
            v = R.u64(x.vals.get(crc("RewardsValue")))
            if v not in (None, R.NONE):
                rv = v
        r = R.resolve(rv, data["db"]) if rv is not None else None
        item_id = R.u64(r.vals.get(crc("PhysicItem"))) if r else None
        if item_id in (None, R.NONE):
            continue
        it = R.resolve(item_id, data["db"])
        slot_ids = [R.u64(v) for k in (it.kid(crc("Slot")).kids if it.kid(crc("Slot")) else [])
                    for v in k.vals.values() if len(v) == 8]
        quality = []
        for medal in ("BronzeMedalQuality", "SilverMedalQuality", "GoldMedalQuality"):
            stack, best = [it.kid(crc(medal))] if it.kid(crc(medal)) else [], (0.0, -1)
            while stack:
                x = stack.pop()
                stack.extend(x.kids)
                if crc("Quality") in x.vals and R.f32(x.vals[crc("Weight")]) > best[0]:
                    best = (R.f32(x.vals[crc("Weight")]), QUALITIES.index(R.u64(x.vals[crc("Quality")])))
            quality.append(best[1])
        sp = it.kid(crc("SpecProbabilities"))
        chances = [R.u32(sp.vals[crc(n)]) for n in ("AccelBonusProba", "TopSpeedBonusProba", "HandlingBonusProba",
                                                     "BrakingBonusProba")] if sp else [25, 25, 25, 25]
        out[f"{mid:#x}"] = {"slot": SLOTS.index(slot_ids[0]), "level": R.u32(it.vals[crc("BSGMedalLevel")]),
                            "quality": quality, "bonus": chances}
    return out


def generate(d: str) -> dict:
    arch = gen_cars.Archetypes(gamefiles.read_file(d, gen_cars.ARCHETYPES))
    root = fcb.parse(gamefiles.read_file(d, DB), resolve=True)
    ids, parts = [], []
    for entry in root.children[0].children:
        pid = next(int.from_bytes(v, "little") for x in gen_cars.walk(entry) for _, v in x.values
                   if isinstance(v, bytes) and len(v) == 8)
        e = arch.by_id[pid]
        bonus = [k for k, f in enumerate(BONUS) if (item(arch, e, f) or 0) > 0]
        level = max(item(arch, e, ITEM_LEVEL) or 0, item(arch, e, CAU_LEVEL) or 0)
        ids.append(f"{pid:#x}")
        parts.append([SLOTS.index(item(arch, e, SLOT_ID)), QUALITIES.index(item(arch, e, QUALITY_ID)), level,
                      bonus[0] if len(bonus) == 1 else (4 if bonus else -1)])
    return {"slots": [f"{s:#x}" for s in SLOTS], "ids": ids, "parts": parts, "missions": mission_parts(d)}


def main() -> None:
    args = sys.argv[1:]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tcreborn", "games", "v1_21", "data", "physparts.json")
    if "-o" in args:
        out = args[args.index("-o") + 1]
        args = args[:args.index("-o")] + args[args.index("-o") + 2:]
    d = gen_cars.data_dir(args[0] if args else None)
    if d is None:
        sys.exit("no game data folder found")
    table = generate(d)
    with open(out, "w") as f:
        json.dump(table, f, separators=(",", ":"))
    print(f"{out}: {len(table['parts'])} performance parts, {len(table['missions'])} missions with a reward part")


if __name__ == "__main__":
    main()
