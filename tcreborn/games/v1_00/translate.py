# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import difflib
import struct

from tcreborn.codec import CONTAINERS, FMT, SIZE, encode_default

KIND = {"u8": "b", "u16": "n", "u32": "n", "u64": "q", "arr": "arr", "list": "blob", "blob": "blob"}


def expand(layout: list[str]) -> list[str]:
    out = []
    for token in layout:
        kind, _, count = token.partition("*")
        out += [kind] * int(count or 1)
    return out


def align(layout121: list[str], layout100: list[str]) -> list[int | None]:
    a = [KIND.get(k, k) for k in expand(layout121)]
    b = [KIND.get(k, k) for k in expand(layout100)]
    out: list[int | None] = [None] * len(b)
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == "equal":
            for k in range(j2 - j1):
                out[j1 + k] = i1 + k
    return out


def decode(tokens: list[str], body: bytes) -> list[int | bytes]:
    values, pos = [], 0
    for kind in tokens:
        if kind in CONTAINERS:
            (n,) = struct.unpack_from(">I", body, pos)
            if pos + 4 + n > len(body):
                raise struct.error("container runs past the body")
            values.append(body[pos:pos + 4 + n])
            pos += 4 + n
        else:
            values.append(struct.unpack_from(FMT[kind], body, pos)[0])
            pos += SIZE[kind]
    return values


def _default(kind: str, first: bool) -> bytes:
    return encode_default([kind]) if kind in CONTAINERS or first else bytes(SIZE[kind])


def encode(tokens: list[str], values: list) -> bytes:
    out = b""
    for k, (kind, value) in enumerate(zip(tokens, values)):
        if value is None:
            out += _default(kind, k == 0)
        elif kind in CONTAINERS:
            out += value if isinstance(value, bytes) else _default(kind, False)
        elif isinstance(value, bytes):
            out += _default(kind, k == 0)
        else:
            out += struct.pack(FMT[kind], value & ((1 << (8 * SIZE[kind])) - 1))
    return out


def transcode(body121: bytes, layout121: list[str], layout100: list[str],
              mapping: list[int | None] | None = None) -> bytes:
    t121, t100 = expand(layout121), expand(layout100)
    values121 = decode(t121, body121)
    mapping = mapping if mapping is not None else align(layout121, layout100)
    return encode(t100, [None if m is None else values121[m] for m in mapping])
