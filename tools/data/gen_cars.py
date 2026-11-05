# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcb
import gamefiles
import lzo1x

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
DATA_DIRS = [os.path.join(ROOT, "GAME", "The Crew", "data_win32"), os.path.join(ROOT, "ps4_dump", "data_orbis")]
ARCHETYPES = "entity\\generated\\archetypes.entities.bin"
SERVER = "entity\\generated\\server_archetypes.entities.bin"
NONE = 0xFFFFFFFFFFFFFFFF

ID, FATHER = 0x11D3633A, 0x07F184FD
MODEL, SPEC, DEFAULT_PART, TIER, SELLABLE = 0x8201C17B, 0x465C641A, 0xD51F8C0C, 0x60E32404, 0x43FCC16F
VARIANT_ROOTS = {0x2308AA3, 0x7937BE5}
STOCK_SPEC = 0x37568E
PRICE = 0xFFE4B082
MODEL_SPECS, MODEL_ID, SPEC_LIST, SPEC_ID = 0x145176AE, 0x16545DDD, 0xC4ABBCFC, 0x7C6D9B85
CONFIG, PART_REF, PART_ID = 0x250ADAE9, 0xB8F8F0A8, 0xB7F8C275
SLOTS = 38
PART_LIST = 0xE635C4913170C07A
SPAWNERS = "project\\road66\\generated\\missionspawners.entities.bin"
DEALER_BUDGET, BUDGET = 0x8A4BD8E6, 0x1096028A
SPECIAL_SPECS = {0x7938316: 400000, 0x7938315: 450000, 0x7938317: 500000}
PRICE_FIXES = {0x37569E: 22500}

SLOT_KINDS = {11: (0x87D8821, 0xE36603C9), 12: (0x87D8821, 0x60159DAA), 13: (0x87D8821, 0xD875B55B),
              14: (0x87D8821, 0x50AE985F), 15: (0x87D8821, 0x1F3FDA40), 16: (0x87D8821, 0x20BF08E2),
              17: (0x87D8821, 0x5B535EFC), 18: (0x87D8821, 0xD820C09F), 19: (0x87D8821, 0xBDBC35F9),
              21: (0xD0B4B975, 0xAAC44A13), 22: (0xD0B4B975, 0xA6141CF5), 23: (0xD0B4B975, 0xB7384406),
              25: (0xD0B4B975, 0xB98202B5), 26: (0xD0B4B975, 0x951F8509), 27: (0xD0B4B975, 0xAAFD3DF4),
              28: (0xF1CE13D3, 0x6C18E473), 29: (0xF1CE13D3, 0x6930C57F), 30: (0xF1CE13D3, 0xC030D41D),
              31: (0xF1CE13D3, 0x50AE985F), 32: (0xF1CE13D3, 0x725E92DC), 33: (0xF1CE13D3, 0xC92B6168),
              34: (0xF1CE13D3, 0x5B535EFC), 35: (0xF1CE13D3, 0x858782BB), 36: (0xF1CE13D3, 0x5F503018),
              37: (0xF1CE13D3, 0xBDBC35F9)}


def data_dir(arg: str | None = None) -> str | None:
    for d in ([arg] if arg else DATA_DIRS):
        if d and os.path.isdir(d):
            return d
    return None


class Archetypes:
    def __init__(self, data: bytes):
        self.root = fcb.parse(data, resolve=True)
        self.by_id = {}
        for e in self.root.children:
            i = self.raw(e, ID)
            if i is not None:
                self.by_id[int.from_bytes(i, "little")] = e

    @staticmethod
    def raw(o, field):
        for k, v in o.values:
            if k == field and isinstance(v, bytes):
                return v
        return None

    def get(self, e, field):
        for _ in range(32):
            if e is None:
                return None
            v = self.raw(e, field)
            if v is not None:
                return int.from_bytes(v, "little")
            f = self.raw(e, FATHER)
            e = self.by_id.get(int.from_bytes(f, "little")) if f else None
        return None

    def root_of(self, i: int) -> int:
        seen = []
        while i in self.by_id and i not in seen:
            seen.append(i)
            f = self.raw(self.by_id[i], FATHER)
            if f is None or int.from_bytes(f, "little") == NONE:
                break
            i = int.from_bytes(f, "little")
        return seen[-1] if seen else i


def walk(o):
    seen, stack = set(), [o]
    while stack:
        x = stack.pop()
        if isinstance(x, fcb.Obj) and id(x) not in seen:
            seen.add(id(x))
            yield x
            stack.extend(x.children)


def model_specs(server: Archetypes) -> set[tuple[int, int]]:
    pairs = set()
    for x in walk(server.root):
        if x.cls == MODEL_SPECS:
            model = server.get(x, MODEL_ID)
            for lst in x.children:
                if isinstance(lst, fcb.Obj) and lst.cls == SPEC_LIST:
                    pairs |= {(model, server.get(s, SPEC_ID)) for s in lst.children if isinstance(s, fcb.Obj)}
    return pairs


def read_hash(d: str, name_hash: int) -> bytes:
    found = None
    for fat_path in gamefiles.archives(d):
        with open(fat_path, "rb") as f:
            fat = f.read()
        magic, version, _flags, count = struct.unpack_from("<4sIII", fat)
        if magic != b"2TAF" or version != 5:
            continue
        for i in range(count):
            packed, _, h, a, b = struct.unpack_from("<IIQII", fat, 0x10 + i * 24)
            if h == name_hash:
                found = (fat_path[:-4] + ".dat", (b << 2) | (a >> 30), a & 0x3FFFFFFF, packed & 3)
    if not found:
        raise FileNotFoundError(f"{name_hash:#x} not in {d}")
    dat, offset, size, method = found
    with open(dat, "rb") as f:
        f.seek(offset)
        data = f.read(size)
    return lzo1x.decompress(data) if method else data


def part_index(data: bytes) -> dict[int, int]:
    root = fcb.parse(data, resolve=True)
    out = {}
    for n, entry in enumerate(root.children[0].children):
        for x in walk(entry):
            v = next((v for _, v in x.values if isinstance(v, bytes) and len(v) == 8), None)
            if v is not None:
                out[int.from_bytes(v, "little")] = n
                break
    return out


def slot_parts(arch: Archetypes, e) -> list[int]:
    kinds = {}
    cfg = next((c for c in e.children if isinstance(c, fcb.Obj) and c.cls == CONFIG), None)
    for group in (cfg.children if cfg else []):
        for kind in (group.children if isinstance(group, fcb.Obj) else []):
            if not isinstance(kind, fcb.Obj):
                continue
            for x in walk(kind):
                if x.cls == PART_REF and arch.raw(x, PART_ID) is not None:
                    kinds.setdefault((group.cls, kind.cls), int.from_bytes(arch.raw(x, PART_ID), "little"))
                    break
    out = [NONE] * SLOTS
    for slot, key in SLOT_KINDS.items():
        out[slot] = kinds.get(key, NONE)
    return out


def spec_price(rank: int, tier: int) -> int:
    return 5000 * rank + 2500 * max(0, rank - 3) + 2500 * (tier - 1)


def spec_prices(variants: dict) -> dict[str, list[int]]:
    count: dict[int, dict[int, int]] = {}
    for v in variants.values():
        if v["spec"] != STOCK_SPEC and v["spec"] not in SPECIAL_SPECS and (v["tier"] or 0) >= 1:
            t = count.setdefault(v["spec"], {})
            t[v["tier"]] = t.get(v["tier"], 0) + 1
    out = {}
    for spec, tiers in count.items():
        n = sum(1 for c in tiers.values() if c >= 10)
        if 1 <= n <= 5:
            out[f"{spec:#x}"] = [spec_price(6 - n, t) for t in range(1, n + 1)]
    for spec, price in SPECIAL_SPECS.items():
        out[f"{spec:#x}"] = [price] * 5
    return dict(sorted(out.items()))


def dealer(spawners: bytes) -> tuple[int, int]:
    root = fcb.parse(spawners, resolve=True)
    for e in root.children:
        if not isinstance(e, fcb.Obj):
            continue
        for x in walk(e):
            if x.cls == DEALER_BUDGET and Archetypes.raw(x, BUDGET) is not None:
                return int.from_bytes(Archetypes.raw(e, ID), "little"), int.from_bytes(Archetypes.raw(x, BUDGET), "little")
    return NONE, 0


def build(arch: Archetypes, server: Archetypes, parts: dict[int, int] | None = None,
          spawners: bytes | None = None) -> dict:
    pairs = model_specs(server)
    variants = {}
    for i, e in arch.by_id.items():
        model, spec = arch.get(e, MODEL), arch.get(e, SPEC)
        if (model, spec) in pairs and arch.get(e, SELLABLE) == 1 and arch.root_of(i) in VARIANT_ROOTS:
            variants[i] = {"model": model, "spec": spec, "default_part": arch.get(e, DEFAULT_PART),
                           "tier": arch.get(e, TIER), "slot_parts": slot_parts(arch, e)}
    stock = {v["model"]: v["default_part"] for v in variants.values()
             if v["spec"] == STOCK_SPEC and v["default_part"] not in (None, NONE)}
    for v in variants.values():
        if v["default_part"] in (None, NONE):
            v["default_part"] = stock.get(v["model"], NONE)
    prices = {}
    for model in {v["model"] for v in variants.values()}:
        e = server.by_id.get(model)
        if e is not None and server.get(e, PRICE) is not None:
            prices[f"{model:#x}"] = PRICE_FIXES.get(model, server.get(e, PRICE))
    mission, budget = dealer(spawners) if spawners else (NONE, 0)
    index = parts or {}
    for v in variants.values():
        v["slots"] = [index.get(p, 0xFFFF) if n >= 11 else 0xFFFF for n, p in enumerate(v["slot_parts"])]
    return {"variants": {f"{i:#x}": {"model": f"{v['model']:#x}", "spec": f"{v['spec']:#x}",
                                     "default_part": f"{v['default_part']:#x}", "tier": v["tier"],
                                     "slot_parts": [f"{p:#x}" for p in v["slot_parts"]], "slots": v["slots"]}
                         for i, v in sorted(variants.items())},
            "prices": dict(sorted(prices.items())),
            "parts": {f"{i:#x}": n for i, n in sorted((parts or {}).items(), key=lambda kv: kv[1])},
            "spec_prices": spec_prices(variants), "special_specs": [f"{s:#x}" for s in SPECIAL_SPECS],
            "budget": budget, "dealer": f"{mission:#x}", "slot_defaults": [0xFFFFFFFF] * SLOTS}


def generate(d: str) -> dict:
    return build(Archetypes(gamefiles.read_file(d, ARCHETYPES)), Archetypes(gamefiles.read_file(d, SERVER)),
                 part_index(read_hash(d, PART_LIST)), gamefiles.read_file(d, SPAWNERS))


def main() -> None:
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    out = sys.argv[sys.argv.index("-o") + 1] if "-o" in sys.argv else None
    if out in args:
        args.remove(out)
    d = data_dir(args[0] if args else None)
    if d is None:
        sys.exit("no game data folder found")
    result = generate(d)
    text = json.dumps(result)
    if out:
        with open(out, "w") as f:
            f.write(text)
    print(f"{len(result['variants'])} variants, {len(result['prices'])} prices, {len(result['parts'])} parts from {d}")


if __name__ == "__main__":
    main()
