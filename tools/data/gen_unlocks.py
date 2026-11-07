# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
import fcb
import gamefiles
from gen_regions import DATA, ROOT
from tcreborn.games.v1_21 import cars
from tcreborn.wire import u64_array
import gen_cars

OUT = os.path.join(ROOT, "tcreborn", "games", "v1_21", "data", "dlc.json")
ID, FATHER, TYPE, PRICE = 0x11D3633A, 0x07F184FD, 0x4FB636DA, 0x22B53BE6
CONTENT = 4
MASK = 0x1FFFFFFFFFFFFFFF


def num(v) -> int | None:
    if isinstance(v, bytes) and len(v) in (4, 8):
        return struct.unpack("<I" if len(v) == 4 else "<Q", v)[0]
    return None


def content_ids(data_dir: str) -> list[int]:
    root = fcb.parse(gamefiles.read_file(data_dir, "entity\\generated\\server_archetypes.entities.bin"), resolve=True)
    ents = {}
    for e in root.children:
        if isinstance(e, fcb.Obj):
            v = dict(e.values)
            if num(v.get(ID)) is not None:
                ents[num(v[ID]) & MASK] = v

    def field(x: int, key: int, depth: int = 0) -> int | None:
        v = ents.get(x)
        if v is None or depth > 8:
            return None
        if key in v:
            return num(v[key])
        father = num(v.get(FATHER))
        return field(father & MASK, key, depth + 1) if father is not None else None

    return sorted(x for x in ents if field(x, TYPE) == CONTENT and not field(x, PRICE))


def pack_items(data_dir: str, ids: list[int], catalog: dict) -> tuple[list[int], list[int]]:
    server = gen_cars.Archetypes(gamefiles.read_file(data_dir, gen_cars.SERVER))
    parts = {int(k, 16): v for k, v in catalog["parts"].items()}
    variants = {int(k, 16) for k in catalog["variants"]}
    bits, packs = [], []
    for x in ids:
        e = server.by_id.get(x)
        for o in (gen_cars.walk(e) if e is not None else []):
            if o is e:
                continue
            for _, v in o.values:
                if isinstance(v, bytes) and len(v) == 8:
                    item = int.from_bytes(v, "little")
                    if item in parts and parts[item] not in bits:
                        bits.append(parts[item])
                    elif item in variants and item not in packs:
                        packs.append(item)
    return bits, packs


def build(data_dir: str) -> dict:
    ids = content_ids(data_dir)
    out = {"source": "game files: server archetypes (tools/data/gen_unlocks.py)",
           "owned_dlc": [f"{x:x}" for x in ids],
           "fields": {"37": u64_array(ids).hex()}}
    catalog = gen_cars.generate(data_dir)
    c = cars.Catalog.from_json({**catalog, "budget": 0, "dealer": "0x0", "slot_defaults": [0xFFFFFFFF] * gen_cars.SLOTS})
    bits, packs = pack_items(data_dir, ids, catalog)
    garage = [(int(c.variants[v]["model"], 16),
               [cars.config(c, v, cars.slots(c, v, cars.NONE, cars.NONE, cars.NONE), 1)]) for v in packs]
    words = [0] * cars.PLAYER_PARTS
    for b in bits:
        words[b >> 6] |= 1 << (b & 63)
    out["fields"]["72"] = cars.garage_field(garage).hex()
    out["fields"]["73"] = cars.player_parts(garage, u64_array(words)).hex()
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", nargs="?", default=DATA)
    ap.add_argument("-o", "--out", default=OUT)
    args = ap.parse_args()
    data = build(args.data_dir)
    with open(args.out, "w") as f:
        json.dump(data, f, indent=1)
    print(f"{args.out}: {len(data['owned_dlc'])} content ids")


if __name__ == "__main__":
    main()
