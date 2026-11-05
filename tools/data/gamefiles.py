# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lzo1x
from bigfile import name_hash


def archives(data_dir: str) -> list[str]:
    dirs = [os.path.join(data_dir, s) for s in ("app0", "patch0") if os.path.isdir(os.path.join(data_dir, s))]
    out = []
    for d in dirs or [data_dir]:
        out += [os.path.join(d, n) for n in sorted(os.listdir(d), key=lambda n: (n.count("_patch"), n))
                if n.endswith(".fat")]
    return out


def read_file(data_dir: str, path: str) -> bytes:
    want, found = name_hash(path), None
    for fat_path in archives(data_dir):
        with open(fat_path, "rb") as f:
            fat = f.read()
        magic, version, _flags, count = struct.unpack_from("<4sIII", fat)
        if magic != b"2TAF" or version != 5:
            continue
        for i in range(count):
            packed, _, h, a, b = struct.unpack_from("<IIQII", fat, 0x10 + i * 24)
            if h == want:
                found = (fat_path[:-4] + ".dat", (b << 2) | (a >> 30), a & 0x3FFFFFFF, packed & 3)
    if not found:
        raise FileNotFoundError(f"{path} not in {data_dir}")
    dat, offset, size, method = found
    with open(dat, "rb") as f:
        f.seek(offset)
        data = f.read(size)
    return lzo1x.decompress(data) if method else data
