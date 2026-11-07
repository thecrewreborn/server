# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcb
import gamefiles
import lzo1x

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(ROOT, "GAME", "The Crew", "data_win32")
OUT = os.path.join(ROOT, "tcreborn", "games", "v1_21", "data", "regions.json")
ID = 0x11D3633A
REGION_SET = 0x78E8C08
REGION_FIELDS = (0xC4D8023D, 0x55F775FA, 0xF971766C, 0x4D3436A3, 0x538B11F7)
LANDMARKS = 0x1259986F73FFCD97
ELEMENT_ID, ELEMENT_REGION = 0xC4C34AAF, 0x9D9AE8C8
CARS = {0x20000000041BAF3C: 0x2522F99, 0x20000000041BC7AA: 0x2F62FB4, 0x20000000041BCEAA: 0x2522F97,
        0x20000000041BCEAB: 0x2522F1D, 0x20000000041C3FDC: 0x2522F9B}


def read_hash(data_dir: str, h: int) -> bytes:
    found = None
    for fat_path in gamefiles.archives(data_dir):
        with open(fat_path, "rb") as f:
            fat = f.read()
        magic, version, _flags, count = struct.unpack_from("<4sIII", fat)
        if magic != b"2TAF" or version != 5:
            continue
        for i in range(count):
            packed, _, hh, a, b = struct.unpack_from("<IIQII", fat, 0x10 + i * 24)
            if hh == h:
                found = (fat_path[:-4] + ".dat", (b << 2) | (a >> 30), a & 0x3FFFFFFF, packed & 3)
    if not found:
        raise FileNotFoundError(f"{h:#x} not in {data_dir}")
    dat, offset, size, method = found
    with open(dat, "rb") as f:
        f.seek(offset)
        data = f.read(size)
    return lzo1x.decompress(data) if method else data


def u64(v) -> int | None:
    return struct.unpack("<Q", v)[0] if isinstance(v, bytes) and len(v) == 8 else None


def regions(data_dir: str) -> list[int]:
    root = fcb.parse(gamefiles.read_file(data_dir, "entity\\generated\\server_archetypes.entities.bin"), resolve=True)
    for e in root.children:
        if isinstance(e, fcb.Obj) and u64(dict(e.values).get(ID)) == REGION_SET:
            v = dict(e.values)
            return [u64(v[k]) for k in REGION_FIELDS]
    raise LookupError("region set not found")


def landmarks(data_dir: str) -> list[tuple[int, int]]:
    root = fcb.parse(read_hash(data_dir, LANDMARKS), resolve=True)
    out, stack, seen = [], [root], set()
    while stack:
        o = stack.pop()
        if not isinstance(o, fcb.Obj) or id(o) in seen:
            continue
        seen.add(id(o))
        v = dict(o.values)
        if ELEMENT_ID in v and ELEMENT_REGION in v:
            out.append((u64(v[ELEMENT_ID]), u64(v[ELEMENT_REGION])))
        stack.extend(o.children)
    return sorted(out)


def build(data_dir: str) -> dict:
    regs, marks = regions(data_dir), landmarks(data_dir)
    per = {r: sum(1 for _, x in marks if x == r) for r in regs}
    if len(set(per.values())) != 1:
        raise ValueError(f"landmarks per region differ: {per}")
    return {"source": "game files: server archetypes and the landmark list (tools/data/gen_regions.py)",
            "threshold": next(iter(per.values())),
            "bits": [hex(r) for _, r in marks],
            "regions": [hex(r) for r in regs],
            "cars": {hex(r): hex(CARS[r]) for r in regs}}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", nargs="?", default=DATA)
    ap.add_argument("-o", "--out", default=OUT)
    args = ap.parse_args()
    data = build(args.data_dir)
    with open(args.out, "w") as f:
        json.dump(data, f, indent=1)
    print(f"{args.out}: {len(data['bits'])} landmarks, {len(data['regions'])} regions")


if __name__ == "__main__":
    main()
