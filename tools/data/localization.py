# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcb
import gamefiles

TEXT, TEXT_ID = 0x29D6A3E8, 0xBF396750
LANGUAGES = "localization\\languages.localization.xml"


def loc_dir(data_dir: str) -> str:
    for d in (os.path.join(data_dir, "pc_ww"), os.path.join(os.path.dirname(os.path.abspath(data_dir)), "loc"),
              data_dir):
        if os.path.isdir(d) and any(n.startswith("localization") and n.endswith(".fat") for n in os.listdir(d)):
            return d
    raise FileNotFoundError(f"no localization bigfiles for {data_dir}")


def languages(data_dir: str) -> dict:
    xml = gamefiles.read_file(loc_dir(data_dir), LANGUAGES).decode("utf-8-sig")
    return {m.group(1): int(m.group(2)) for m in re.finditer(r'name="([^"]+)" id="(\d+)"', xml)}


class Strings:
    def __init__(self, data_dir: str, language: str = "English"):
        lang = languages(data_dir)[language]
        root = fcb.parse(gamefiles.read_file(loc_dir(data_dir), f"localization\\{lang}\\{lang}.localization.bin"),
                         resolve=True)
        self.texts = {}
        for o in root.children:
            v = dict(o.values) if isinstance(o, fcb.Obj) else {}
            if isinstance(v.get(TEXT_ID), bytes) and isinstance(v.get(TEXT), bytes):
                self.texts[struct.unpack("<I", v[TEXT_ID])[0]] = v[TEXT].decode("utf-16-le").rstrip("\0")

    def lookup(self, text_id: int) -> str | None:
        return self.texts.get(text_id)


if __name__ == "__main__":
    print(Strings(sys.argv[1], *sys.argv[3:4]).lookup(int(sys.argv[2], 0)))
