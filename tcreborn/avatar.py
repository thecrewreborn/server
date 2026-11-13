# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import collections
import functools
import hashlib
import os
import re
import struct
import threading
import zlib

from tcreborn import paths
from tcreborn.files import write_atomic
from tcreborn.limits import RateLimit

FOLDER = paths.home("save", "avatars")
LOGO = paths.bundled("avatar_logo.png")
FIT = 256
USER_PATH = re.compile(r"/avatars/(0x[0-9A-F]{8})\.png")
NAME_PATH = re.compile(r"/avatars/name/([A-Za-z0-9_-]{3,16})\.png")
CONSOLE_FOLDER = "ps4"
MAX_UPLOAD = 300_000
MAX_SIDE = 1024
RGBA_CACHE_BYTES = 32 * 2 ** 20
DECODES_PER_S = 10


def _chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


@functools.cache
def logo() -> bytes:
    with open(LOGO, "rb") as f:
        return f.read()


def _encode(width: int, height: int, rgba: bytes) -> bytes:
    stride = 4 * width
    rows = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(rows, 9)) + _chunk(b"IEND", b""))


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def _decode(data: bytes) -> tuple[int, int, bytes]:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    at, idat, head = 8, b"", None
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", data[at + 8:at + 21])
        elif kind == b"IDAT":
            idat += data[at + 8:at + 8 + n]
        at += 12 + n
    width, height, depth, color, _, _, interlace = head
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color)
    if depth != 8 or interlace or not channels:
        raise ValueError(f"unsupported PNG (depth {depth}, color {color}, interlace {interlace})")
    if width > MAX_SIDE or height > MAX_SIDE:
        raise ValueError(f"picture too large ({width}x{height})")
    stride = channels * width
    raw = zlib.decompressobj().decompress(idat, height * (stride + 1))
    out, prev = bytearray(), bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            c = prev[i - channels] if i >= channels else 0
            line[i] = (line[i] + (0, a, prev[i], (a + prev[i]) // 2, _paeth(a, prev[i], c))[kind]) & 0xFF
        out += line
        prev = line
    if channels == 4:
        return width, height, bytes(out)
    rgba = bytearray()
    for i in range(0, len(out), channels):
        px = out[i:i + channels]
        rgba += {1: lambda: px * 3 + b"\xff", 2: lambda: px[:1] * 3 + px[1:],
                 3: lambda: px + b"\xff"}[channels]()
    return width, height, bytes(rgba)


def _box(width: int, height: int, rgba: bytes, size: int) -> bytes:
    out = bytearray()
    for ty in range(size):
        y0, y1 = ty * height // size, max(ty * height // size + 1, (ty + 1) * height // size)
        for tx in range(size):
            x0, x1 = tx * width // size, max(tx * width // size + 1, (tx + 1) * width // size)
            sums, n = [0, 0, 0, 0], (y1 - y0) * (x1 - x0)
            for y in range(y0, y1):
                row = rgba[4 * (y * width + x0):4 * (y * width + x1)]
                for ch in range(4):
                    sums[ch] += sum(row[ch::4])
            out += bytes(s // n for s in sums)
    return bytes(out)


def fit(data: bytes) -> bytes:
    width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if width > MAX_SIDE or height > MAX_SIDE:
        raise ValueError(f"picture too large ({width}x{height})")
    square = width == height and width & (width - 1) == 0 and width <= FIT
    if data[:8] == b"\x89PNG\r\n\x1a\n" and square and (depth, color, interlace) == (8, 6, 0):
        return data
    width, height, rgba = _decode(data)
    if square:
        return _encode(width, height, rgba)
    return _encode(FIT, FIT, _box(width, height, rgba, FIT))


class _RgbaCache:

    def __init__(self, budget: int):
        self.budget, self.size = budget, 0
        self.entries: collections.OrderedDict[tuple[bytes, int], bytes] = collections.OrderedDict()
        self.lock = threading.Lock()

    def get(self, key):
        with self.lock:
            rgba = self.entries.get(key)
            if rgba is not None:
                self.entries.move_to_end(key)
            return rgba

    def put(self, key, rgba: bytes) -> None:
        with self.lock:
            old = self.entries.pop(key, None)
            self.size -= len(old) if old is not None else 0
            self.entries[key] = rgba
            self.size += len(rgba)
            while self.size > self.budget and len(self.entries) > 1:
                self.size -= len(self.entries.popitem(last=False)[1])


_RGBA = _RgbaCache(RGBA_CACHE_BYTES)
_DECODES = RateLimit(DECODES_PER_S, 1.0)


def _rgba(png: bytes, size: int, client_ip: str | None = None) -> bytes | None:
    key = (hashlib.sha256(png).digest(), size)
    rgba = _RGBA.get(key)
    if rgba is not None:
        return rgba
    if client_ip is not None and not _DECODES.allow(client_ip):
        return None
    width, height, rgba = _decode(png)
    rgba = rgba if width == height == size else _box(width, height, rgba, size)
    _RGBA.put(key, rgba)
    return rgba


def save_upload(name: str, rgba: bytes) -> bool:
    if len(rgba) != FIT * FIT * 4 or os.path.basename(name) != name:
        return False
    os.makedirs(FOLDER, exist_ok=True)
    path = os.path.join(FOLDER, name + ".png")
    write_atomic(path, _encode(FIT, FIT, rgba))
    return True


def rgba_for(name: str | None, size: int, client_ip: str) -> bytes | None:
    path = f"/avatars/name/{name}.png" if name else ""
    return _rgba(for_request(name, path), size, client_ip)


@functools.lru_cache(maxsize=16)
def _fitted(path: str, mtime: float) -> bytes:
    with open(path, "rb") as f:
        return fit(f.read())


def check_console_picture(data: bytes, decode: bool = True) -> str | None:
    if len(data) > MAX_UPLOAD:
        return f"picture larger than {MAX_UPLOAD} bytes"
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR" or len(data) < 33:
        return "not a PNG"
    width, height = struct.unpack(">II", data[16:24])
    if width > MAX_SIDE or height > MAX_SIDE:
        return f"picture larger than {MAX_SIDE}x{MAX_SIDE}"
    if not decode:
        return None
    try:
        _decode(data)
    except (ValueError, KeyError, IndexError, TypeError, struct.error, zlib.error):
        return "unsupported PNG"
    return None


def _valid_name(name: str | None) -> bool:
    return bool(name) and os.path.basename(name) == name


def console_path(name: str) -> str:
    return os.path.join(FOLDER, CONSOLE_FOLDER, name + ".png")


def save_console_upload(name: str, data: bytes) -> str | None:
    if not _valid_name(name):
        return "bad name"
    why = check_console_picture(data)
    if why:
        return why
    os.makedirs(os.path.join(FOLDER, CONSOLE_FOLDER), exist_ok=True)
    write_atomic(console_path(name), data)
    return None


def for_request(name: str | None, path: str) -> bytes:
    own_profile = USER_PATH.fullmatch(path) is not None
    if path.startswith("/avatars/name/"):
        other = NAME_PATH.fullmatch(path)
        name = other.group(1) if other else None
    if _valid_name(name):
        pc, console = os.path.join(FOLDER, name + ".png"), console_path(name)
        for file in (console, pc) if own_profile else (pc, console):
            if not os.path.exists(file):
                continue
            try:
                return _fitted(file, os.path.getmtime(file))
            except (OSError, ValueError, KeyError, IndexError, TypeError, struct.error, zlib.error):
                pass
    return logo()
