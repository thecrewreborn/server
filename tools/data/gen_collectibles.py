# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcb
from gen_regions import DATA, ROOT, read_hash

OUT = os.path.join(ROOT, "tcreborn", "games", "v1_21", "data", "collectibles.json")
COLLECTIBLES = 0xDF65B0AE78916BB3
ID, TO_NODE, TO_ENTRY, SET = 0x11D3633A, 0x853C7235, 0x556B25A2, 0xB08B2324


def u64(v) -> int | None:
    return struct.unpack("<Q", v)[0] if isinstance(v, bytes) and len(v) == 8 else None


def build(data_dir: str) -> dict:
    root = fcb.parse(read_hash(data_dir, COLLECTIBLES), resolve=True)
    ents = {u64(dict(e.values).get(ID)): dict(e.values) | {"_": e} for e in root.children if isinstance(e, fcb.Obj)}

    def set_id(entry: int | None) -> int | None:
        e = ents.get(entry)
        if e is None:
            return None
        for c in e["_"].children:
            s = u64(dict(c.values).get(SET)) if isinstance(c, fcb.Obj) else None
            if s is not None:
                return s
        return None

    set_of = {}
    for cid, e in sorted(ents.items(), key=lambda kv: kv[0] or 0):
        node = ents.get(u64(e.get(TO_NODE)))
        s = set_id(u64(node.get(TO_ENTRY))) if node else None
        if s is not None:
            set_of[cid] = s
    needed = collections.Counter(set_of.values())
    return {"source": "game files: the collectible list (tools/data/gen_collectibles.py)",
            "set_of": {hex(c): hex(s) for c, s in set_of.items()},
            "needed": {hex(s): n for s, n in sorted(needed.items())}}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", nargs="?", default=DATA)
    ap.add_argument("-o", "--out", default=OUT)
    args = ap.parse_args()
    data = build(args.data_dir)
    with open(args.out, "w") as f:
        json.dump(data, f, indent=1)
    print(f"{args.out}: {len(data['set_of'])} collectibles in {len(data['needed'])} sets")


if __name__ == "__main__":
    main()
