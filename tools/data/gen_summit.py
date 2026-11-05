# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcb
import lzo1x
from gamefiles import read_file
from localization import Strings

ARCHETYPES = "entity\\generated\\server_archetypes.entities.bin"
ENTITY, RECORD, EVENTS, ENTRY = 0x984415e, 0x76d1e9fb, 0x542b527c, 0x6b5f8c86
START, END = 0xbb44ba11, 0x38b17551
F_ID, F_NAME, F_MONTHLY, F_EVENT, F_FATHER = 0x88981a10, 0x4efd8cbd, 0x8413747e, 0x8f264b0, 0x7f184fd
F_DAY, F_MONTH, F_YEAR, F_HOUR = 0xdded6f70, 0x4f773f02, 0x1bb0dc09, 0xd02cbe70
F_TEXT = 0xeaf7576f
NO_ID = 0xFFFFFFFFFFFFFFFF


def load(source: str) -> bytes:
    if os.path.isfile(source):
        with open(source, "rb") as f:
            data = f.read()
        return data if data[:4] == b"nbCF" else lzo1x.decompress(data)
    return read_file(source, ARCHETYPES)


def _val(o: fcb.Obj, key: int) -> bytes | None:
    return next((v for k, v in o.values if k == key and isinstance(v, bytes)), None)


def _num(o: fcb.Obj, key: int, size: int) -> int:
    v = _val(o, key)
    return int.from_bytes(v[:size], "little") if v else 0


def _date(record: fcb.Obj, cls: int) -> list[int]:
    o = next(c for c in record.children if isinstance(c, fcb.Obj) and c.cls == cls)
    return [_num(o, F_DAY, 1), _num(o, F_MONTH, 1), _num(o, F_YEAR, 2), _num(o, F_HOUR, 1)]


def _events(record: fcb.Obj) -> list[str]:
    out = []

    def walk(o):
        if not isinstance(o, fcb.Obj):
            return
        if o.cls == ENTRY:
            if _val(o, F_EVENT):
                out.append(f"{_num(o, F_EVENT, 8):#018x}")
            return
        for c in o.children:
            walk(c)

    for c in record.children:
        if isinstance(c, fcb.Obj) and c.cls == EVENTS:
            walk(c)
    return out


def records(archetypes: bytes, strings: Strings | None = None) -> list[dict]:
    root, found, seen = fcb.parse(archetypes, resolve=True), [], set()
    stack = [(root, False)]
    while stack:
        o, derived = stack.pop()
        if id(o) in seen:
            continue
        seen.add(id(o))
        if o.cls == ENTITY:
            derived = _num(o, F_FATHER, 8) not in (0, NO_ID)
        if (derived and o.cls == RECORD and _num(o, F_ID, 8) not in (0, NO_ID)
                and any(isinstance(c, fcb.Obj) and c.cls == EVENTS for c in o.children)):
            found.append(o)
        stack.extend((c, derived) for c in reversed(o.children) if isinstance(c, fcb.Obj))
    def name(r):
        text = strings.lookup(_num(r, F_TEXT, 4)) if strings else None
        return text if text is not None else f"SUMMIT {_num(r, F_NAME, 4):08X}"

    return [{"name": name(r), "name_id": f"{_num(r, F_NAME, 4):#010x}",
             "start": _date(r, START), "end": _date(r, END), "monthly": _num(r, F_MONTHLY, 4) == 1,
             "events": _events(r)} for r in found]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("-o", "--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "tcreborn", "data",
                                                        "summit.json"))
    args = ap.parse_args()
    recs = records(load(args.source), Strings(args.source) if os.path.isdir(args.source) else None)
    with open(args.out, "w") as f:
        json.dump({"records": recs}, f, indent=1)
    print(f"{len(recs)} summit records -> {args.out}")


if __name__ == "__main__":
    main()
