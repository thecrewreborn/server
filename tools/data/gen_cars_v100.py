# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import fcb
import gamefiles
import gen_cars
import gen_physparts


def generate(data_dir: str) -> dict:
    with tempfile.TemporaryDirectory() as base:
        os.symlink(os.path.join(data_dir, "app0"), os.path.join(base, "app0"))
        arch = gen_cars.Archetypes(gamefiles.read_file(base, gen_cars.ARCHETYPES))
        parts = gen_cars.part_index(gen_cars.read_hash(base, gen_cars.PART_LIST))
        server = gen_cars.Archetypes(gamefiles.read_file(data_dir, gen_cars.SERVER))
        spawners = gamefiles.read_file(data_dir, gen_cars.SPAWNERS)
        cat = gen_cars.build(arch, server, parts, spawners)
        perf = fcb.parse(gamefiles.read_file(base, gen_physparts.DB), resolve=True).children[0].children
    return {"parts": len(parts), "perf_parts": len(perf), "dealer": cat["dealer"], "budget": cat["budget"],
            "slots": {k: v["slots"][11:25] for k, v in sorted(cat["variants"].items())}}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", nargs="?", default=os.path.join(ROOT, "ps4_dump", "data_orbis"))
    ap.add_argument("-o", default=os.path.join(ROOT, "tcreborn", "games", "v1_00", "data", "cars.json"))
    a = ap.parse_args()
    out = generate(a.data_dir)
    with open(a.o, "w") as f:
        json.dump(out, f, indent=0, sort_keys=True)
    print(f"{len(out['slots'])} variants, {out['parts']} parts -> {a.o}")


if __name__ == "__main__":
    main()
