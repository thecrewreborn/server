# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct
import sys
import zlib
from dataclasses import dataclass, field

KNOWN = {zlib.crc32(n.encode()): n for n in ("Model", "Price", "hidName", "text_hidName", "Name", "Id")}


@dataclass
class Obj:
    cls: int
    values: list[tuple[int, bytes | int]]
    children: list["Obj | int"] = field(default_factory=list)


def parse(data: bytes, resolve: bool = False) -> Obj:
    pos = 16
    objs: list[Obj] = []
    vals: dict[int, bytes] = {}

    def count() -> tuple[int, int | None]:
        nonlocal pos
        start, b = pos, data[pos]
        pos += 1
        if b < 0xFE:
            return b, None
        (v,) = struct.unpack_from("<I", data, pos)
        pos += 4
        return (0, start - v) if b == 0xFE else (v, None)

    def obj() -> Obj | int:
        nonlocal pos
        start = pos
        n, ref = count()
        if ref is not None:
            index = start - ref
            return objs[index] if resolve and index < len(objs) else ref
        (cls,) = struct.unpack_from("<I", data, pos)
        pos += 4
        nv, _ = count()
        values = []
        for _ in range(nv):
            (name,) = struct.unpack_from("<I", data, pos)
            pos += 4
            vat = pos
            size, ref = count()
            if ref is not None:
                values.append((name, vals.get(ref, ref) if resolve else ref))
            else:
                v = data[pos:pos + size]
                vals[vat] = v
                values.append((name, v))
                pos += size
        o = Obj(cls, values)
        objs.append(o)
        o.children = [obj() for _ in range(n)]
        return o

    root = obj()
    assert isinstance(root, Obj)
    return root


def find_ids(root: Obj, ids: set[int]):
    stack = [(root, [])]
    while stack:
        o, path = stack.pop()
        for _, v in o.values:
            if isinstance(v, bytes) and len(v) == 8 and struct.unpack("<Q", v)[0] in ids:
                yield o, path
                break
        stack.extend((c, path + [o.cls]) for c in o.children if isinstance(c, Obj))


def _show(v) -> str:
    if not isinstance(v, bytes):
        return f"-> {v:#x}"
    s = v.rstrip(b"\0")
    return s.decode("latin1") if len(s) > 2 and all(32 <= c < 127 for c in s) else v[:16].hex()


def main() -> None:
    root = parse(open(sys.argv[1], "rb").read())
    for o, path in find_ids(root, {int(x, 16) for x in sys.argv[2:]}):
        print(f"class {o.cls:#010x} path {[hex(p) for p in path]}")
        for name, v in o.values:
            print(f"    {KNOWN.get(name, hex(name))}: {_show(v)}")


if __name__ == "__main__":
    main()
