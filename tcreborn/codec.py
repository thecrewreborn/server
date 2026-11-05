# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.wire import blob, empty_array, serializer_list

SIZE = {"u8": 1, "u16": 2, "u32": 4, "u64": 8}
FMT = {"u8": ">B", "u16": ">H", "u32": ">I", "u64": ">Q"}
CONTAINERS = ("arr", "list", "blob")


def _split(token: str) -> tuple[str, int]:
    kind, _, count = token.partition("*")
    return kind, int(count or 1)


def _empty(kind: str) -> bytes:
    return {"arr": empty_array(), "list": blob(serializer_list([])), "blob": blob(b"")}[kind]


def encode_default(layout: list[str], overrides: dict[int, bytes] | None = None) -> bytes:
    overrides = overrides or {}
    out = b""
    for k, token in enumerate(layout):
        if k in overrides:
            out += overrides[k]
            continue
        kind, count = _split(token)
        if kind in CONTAINERS:
            out += _empty(kind) * count
        elif k == 0 and kind == "u8":
            out += b"\x01" + b"\0" * (count - 1)
        else:
            out += b"\0" * SIZE[kind] * count
    return out


def decode(layout: list[str], body: bytes) -> list[int | bytes]:
    values: list[int | bytes] = []
    pos = 0
    for token in layout:
        kind, count = _split(token)
        for _ in range(count):
            if kind in CONTAINERS:
                (n,) = struct.unpack_from(">I", body, pos)
                values.append(body[pos + 4:pos + 4 + n])
                pos += 4 + n
            else:
                (v,) = struct.unpack_from(FMT[kind], body, pos)
                values.append(v)
                pos += SIZE[kind]
    return values
