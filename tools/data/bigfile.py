# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os
import struct
import sys


def name_hash(path: str) -> int:
    crc = 0
    for b in path.lower().replace("/", "\\").encode("ascii"):
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x95AC9329AC4BC9B5 if crc & 1 else crc >> 1
    return crc


def entries(fat_path: str) -> list[tuple[int, int, int]]:
    with open(fat_path, "rb") as f:
        fat = f.read()
    magic, version, _flags, count = struct.unpack_from("<4sIII", fat)
    assert magic == b"2TAF" and version == 5, (magic, version)
    out = []
    for i in range(count):
        _zero, h, a, b = struct.unpack_from("<QQII", fat, 0x10 + i * 24)
        out.append((h, (b << 2) | (a >> 30), a & 0x3FFFFFFF))
    return out


def read(dat_path: str, offset: int, size: int) -> bytes:
    with open(dat_path, "rb") as f:
        f.seek(offset)
        return f.read(size)


def index(data_dir: str) -> dict[int, tuple[str, int, int]]:
    out = {}
    for sub in ("app0", "patch0"):
        d = os.path.join(data_dir, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d), key=lambda n: (n.count("_patch"), n)):
            if name.endswith(".fat"):
                dat = os.path.join(d, name[:-4] + ".dat")
                for h, off, size in entries(os.path.join(d, name)):
                    out[h] = (dat, off, size)
    return out


if __name__ == "__main__":
    base = sys.argv[1]
    for h, off, size in sorted(entries(base + ".fat"), key=lambda e: e[1]):
        print(f"{h:016x} {off:#010x} {size:>10} {read(base + '.dat', off, 16).hex()}")
