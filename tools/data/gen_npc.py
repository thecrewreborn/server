# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamefiles
from gen_cars import ARCHETYPES, FATHER, NONE, Archetypes, data_dir

PART_MODEL = 0x309FE375
CATALOG_ROOTS = {1: {0x7B2C1, 0xC2BEFD, 0xC2BEFE, 0x78513F7, 0x7938999, 0x7939477}, 2: {0x2CDAC3E, 0x7937BE3}}
KIT, KIT_CLASS = 0x7CC1C83B, 0xD36A08A1
CLASS_FIELD = {0x7B2C1: 0xF56303FE, 0xC2BEFD: 0x33D396C1, 0xC2BEFE: 0x84EFADDE, 0x7938999: 0x52091693,
               0x7939477: 0x39745D8C}
KITS = {"Road": 2500, "Off-Road": 5000, "Racing": 10000, "Official": 10000, "Exotic": 20000, "Vintage": 25000,
        "KTMOfficialStreet": 10000, "KTMOfficial_FSDTRD": 10000, "Kawasaki450OfficialStreet": 10000,
        "Kawasaki450Official_FSDTRD": 10000}
BASE = {
    0x7B2C1: {"Aluminium": 500, "Carbone": 1000, "Wood": 2000, "Official": 2000, "Lakered": 4000, "Mixed_Alu": 5000,
              "Mixed_Carbon": 6000},
    0xC2BEFD: {"Metallic": 2500, "Matte": 5000, "Official_f": 5000, "Anodized": 10000, "Chrome": 15000,
               "Chameleon": 20000},
    0xC2BEFE: {"Flaming": 5000, "Racing": 5000, "Urban": 10000, "Tribal": 10000, "Nature": 20000, "Animals": 20000,
               "Skull": 40000, "Vintage": 40000, "Christmas": 45000, "Flags": 50000, "Surf": 50000, "Ubisoft": 120000},
    0x7938999: {"TopType_Street_Only": 70000, "TopType_Raid_Only": 65000, "TopType_Dirt_Only": 75000,
                "TopType_Perf_Only": 90000, "BottomType_Street_Only": 30000, "BottomType_Dirt_Raid": 30000,
                "BottomType_Perf_Only": 40000, "CombiType_Race_Only": 40000},
    0x7939477: {"HelmetType_Street_Only": 50000, "HelmetType_Dirt_Raid": 50000, "HelmetType_Perf_Only": 50000,
                "HelmetType_Race_Only": 50000},
    0x2CDAC3E: KITS, 0x7937BE3: KITS,
}


def label(arch: Archetypes, cid: int | None) -> str | None:
    e = arch.by_id.get(cid) if cid not in (None, NONE) else None
    names = [v.rstrip(b"\0").decode() for _, v in (e.values if e else []) if isinstance(v, bytes)
             and len(v.rstrip(b"\0")) >= 4 and all(32 <= c < 127 for c in v.rstrip(b"\0"))]
    return names[-1] if names else None


def kit_class(arch: Archetypes, i: int) -> int | None:
    for _ in range(32):
        e = arch.by_id.get(i)
        if e is None:
            return None
        for c in e.children:
            if getattr(c, "cls", None) == KIT:
                v = arch.raw(c, KIT_CLASS)
                if v is not None:
                    return int.from_bytes(v, "little")
        f = arch.raw(e, FATHER)
        if f is None:
            return None
        i = int.from_bytes(f, "little")
    return None


def price(arch: Archetypes, root: int, i: int, e) -> int:
    cid = kit_class(arch, i) if root in (0x2CDAC3E, 0x7937BE3) else arch.get(e, CLASS_FIELD[root]) if root in CLASS_FIELD \
        else None
    return BASE.get(root, {}).get(label(arch, cid), 0)


def build(arch: Archetypes) -> dict:
    catalog_of = {r: n for n, roots in CATALOG_ROOTS.items() for r in roots}
    items = {}
    for i, e in sorted(arch.by_id.items()):
        root = arch.root_of(i)
        n = catalog_of.get(root)
        if n is None or i in catalog_of:
            continue
        model = arch.get(e, PART_MODEL)
        items[f"{i:#x}"] = {"catalog": n, "price": price(arch, root, i, e),
                            "model": f"{model:#x}" if n == 1 and model not in (None, 0, NONE) else None,
                            "config": False}
    return {"items": items}


def generate(d: str) -> dict:
    return build(Archetypes(gamefiles.read_file(d, ARCHETYPES)))


def main() -> None:
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    out = sys.argv[sys.argv.index("-o") + 1] if "-o" in sys.argv else None
    if out in args:
        args.remove(out)
    d = data_dir(args[0] if args else None)
    if d is None:
        sys.exit("no game data folder found")
    result = generate(d)
    if out:
        with open(out, "w") as f:
            json.dump(result, f)
    print(f"{len(result['items'])} items from {d}")


if __name__ == "__main__":
    main()
