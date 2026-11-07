# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import json
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcb
import gamefiles

ARCHETYPES = "entity\\generated\\archetypes.entities.bin"
SERVER = ("entity\\generated\\server_archetypes.entities.bin", "entity\\generated\\server_heavy_archetypes.entities.bin")
SPAWNERS = "project\\road66\\generated\\missionspawners.entities.bin"
TAGS = "project\\road66\\generated\\TagDB.bin"

ID, FATHER = 0x11D3633A, 0x07F184FD
REWARD_DATA, REWARD_VALUES, OBJECTIVES = 0x8A4BD8E6, 0xD0EFFE10, 0x23ED6ABC
MEDALS = (0xBEE8B989, 0x2F5B185B, 0x47A91D51, 0x2FB23C6D)
MAX_LEVEL, DELOCK_LEVEL, REWARDING_XP = 0x8003D4A2, 0xCC2069F9, 0x3EB12DED
XP, BUCKS, REPUTATION, ESK = 0x9B629638, 0x4F72CEA3, 0xAF2F0DBC, 0xB4FFCFD5
SMART_LOOT, LOOT = 0xA605D836, (0xDEB72BFF, 0x78037C23, 0xC13933D8, 0x1261BB65)
MEDAL, NO_MEDAL = 0x1D8578BD, 0xA0EF8C0D
SOURCE, ZONE, PRESET = 0xC4C34AAF, 0x9D9AE8C8, 0x4DF3F12C
TAG_MEDALS = (0x093133B9, 0x994CC18C, 0x534F47C9, 0xFEC0145C)
ONLINE_TYPE, HUD, RACE_TYPE = 0x3DB5C587, 0x509B1713, 0x688EBB71
ABILITY_DATA, CLASS_NAME = 0x0E91E94C, 0x25368426
KINDS = {4: 2, 6: 2, 9: 4, 11: 6, 12: 6}
ECONOMY = 0x1E036
PVE_XP, PVE_BUCKS, LANDMARK_XP, LANDMARK_BUCKS = 0x1153B55C, 0x3D99A026, 0x33401844, 0xAB5A6188
BY_LEVEL = (0xF91291FF, 0xE79467E4, 0x108AD464, 0xCF9C7B22)
MEDAL_BONUS = (0x552C8B5F, 0xCEC4DB48, 0x8CAD5872, 0x23DE82A4)
MEDAL_STEPS = (0x1CBEA960, 0xB51E002B, 0xD314AEA9, 0xF5FB0A85)
CAPS = (9999999, 99999999, 50, 100000)
NONE = 0xFFFFFFFFFFFFFFFF


class Obj:
    __slots__ = ("cls", "vals", "kids")

    def __init__(self, cls):
        self.cls, self.vals, self.kids = cls, {}, []

    def kid(self, cls):
        return next((k for k in self.kids if k.cls == cls), None)


def convert(o, memo: dict) -> Obj:
    if id(o) in memo:
        return memo[id(o)]
    out = memo[id(o)] = Obj(o.cls)
    out.vals = {n: d for n, d in o.values if isinstance(d, bytes)}
    out.kids = [convert(k, memo) for k in o.children if not isinstance(k, int)]
    return out


def parse(data: bytes) -> Obj:
    return convert(fcb.parse(data, resolve=True), {})


def u32(b):
    return struct.unpack("<I", b)[0]


def f32(b):
    return struct.unpack("<f", b)[0]


def u64(b):
    return struct.unpack("<Q", b)[0] if b is not None and len(b) == 8 else None


def entities(root: Obj) -> dict:
    return {u64(o.vals[ID]): o for o in root.kids if ID in o.vals}


def merged(dst: Obj, src: Obj) -> Obj:
    dst.vals.update(src.vals)
    for k in src.kids:
        cur = dst.kid(k.cls)
        if cur is None:
            cur = Obj(k.cls)
            dst.kids.append(cur)
        merged(cur, k)
    return dst


def resolve(eid: int, db: dict, depth: int = 0) -> Obj | None:
    o = db.get(eid)
    if o is None or depth > 32:
        return None
    father = u64(o.vals.get(FATHER))
    base = resolve(father, db, depth + 1) if father not in (None, NONE) else None
    out = merged(Obj(o.cls), base) if base else Obj(o.cls)
    return merged(out, o)


def load(data_dir: str) -> dict:
    def file(path):
        return parse(gamefiles.read_file(data_dir, path))

    db = {}
    for name in (ARCHETYPES,) + SERVER:
        db.update(entities(file(name)))
    return {"db": db, "spawners": entities(file(SPAWNERS)), "tags": file(TAGS), "names": ability_names(data_dir)}


def reward_words(rv: Obj) -> list[int]:
    sl, medal = rv.kid(SMART_LOOT) or Obj(0), rv.kid(MEDAL) or Obj(0)
    words = [rv.vals.get(h) for h in (XP, BUCKS, REPUTATION, ESK)] + [sl.vals.get(h) for h in LOOT]
    return [u32(b) if b else 0 for b in words + [medal.vals.get(NO_MEDAL)]]


def ability_names(data_dir: str) -> dict:
    up = os.path.dirname(os.path.abspath(data_dir))
    names = {}
    for exe in ("TheCrew.exe", "eboot.bin"):
        path = os.path.join(up, exe)
        if os.path.exists(path):
            with open(path, "rb") as f:
                for m in re.finditer(rb"CAbility_[A-Za-z0-9_]+", f.read()):
                    names[zlib.crc32(m.group())] = m.group().decode()
    return names


def own_abilities(eid: int, db: dict) -> list[int]:
    for _ in range(32):
        o = db.get(eid)
        if o is None:
            return []
        ad = o.kid(ABILITY_DATA)
        if ad is not None:
            stack, out = [ad], []
            while stack:
                x = stack.pop(0)
                if CLASS_NAME in x.vals and x is not ad:
                    out.append(u32(x.vals[CLASS_NAME]))
                stack[:0] = x.kids
            if out:
                return out
        eid = u64(o.vals.get(FATHER))
    return []


def missions(data: dict) -> tuple[dict, dict]:
    db = {**data["db"], **data["spawners"]}
    out, rvs = {}, {}
    for mid in data["spawners"]:
        e = resolve(mid, db)
        rd = e.kid(REWARD_DATA) if e else None
        if rd is None or REWARD_VALUES not in rd.vals:
            continue
        ob = rd.kid(OBJECTIVES)
        rv = u64(rd.vals[REWARD_VALUES])
        hud = e.kid(HUD)
        race = u32(hud.vals[RACE_TYPE]) if hud and RACE_TYPE in hud.vals else 0
        online = u32(e.vals[ONLINE_TYPE]) if ONLINE_TYPE in e.vals else 0
        out[f"{mid:#x}"] = {
            "type": KINDS.get(online, 1 if race == 7 else 0),
            "factor": 1.0,
            "min_level": u32(e.vals[DELOCK_LEVEL]) if DELOCK_LEVEL in e.vals else 0,
            "max_level": u32(e.vals[MAX_LEVEL]) if MAX_LEVEL in e.vals else 0,
            "rewarding": e.vals.get(REWARDING_XP, b"\0")[0],
            "thresholds": [u32(ob.vals[h]) for h in MEDALS] if ob and all(h in ob.vals for h in MEDALS) else [],
            "reward_values": f"{rv:#x}",
            "abilities": [data["names"].get(h, f"{h:#x}") for h in own_abilities(mid, db)],
        }
        if rv not in rvs and (r := resolve(rv, data["db"])):
            rvs[rv] = reward_words(r)
    return out, {f"{k:#x}": v for k, v in rvs.items()}


def activities(data: dict) -> tuple[dict, dict]:
    acts, families = {}, {}
    stack = [data["tags"]]
    while stack:
        o = stack.pop()
        stack.extend(o.kids)
        src, zone, preset = (u64(o.vals.get(h)) for h in (SOURCE, ZONE, PRESET))
        custom = next((k for k in o.kids if all(h in k.vals for h in TAG_MEDALS)), None)
        if src is None or custom is None:
            continue
        preset = u64(custom.vals.get(PRESET)) or preset
        acts[f"{src:#x}"] = {"region": f"{zone:#x}", "family": f"{preset:#x}",
                             "thresholds": [u32(custom.vals[h]) for h in TAG_MEDALS]}
        if preset not in families and (p := resolve(preset, data["db"])):
            rv = resolve(u64(p.vals.get(REWARD_VALUES)), data["db"]) if REWARD_VALUES in p.vals else None
            if rv and XP in rv.vals and BUCKS in rv.vals:
                families[preset] = [f32(rv.vals[XP]), f32(rv.vals[BUCKS])]
    return acts, {f"{k:#x}": v for k, v in families.items()}


def table(data: dict) -> list[int]:
    e = resolve(ECONOMY, data["db"])
    rows = [list(e.kid(h).vals.values()) for h in BY_LEVEL]
    medals = [e.kid(g).vals[h] for g in MEDAL_BONUS for h in MEDAL_STEPS]
    head = [len(rows[3]), *CAPS] + [u32(b) for b in [e.vals[PVE_XP], e.vals[PVE_BUCKS], *medals]]
    return head + [u32(e.vals[LANDMARK_XP]), u32(e.vals[LANDMARK_BUCKS])] + [u32(b) for r in rows for b in r]


def ps4_bits(code: bytes) -> dict:
    bits = {}
    for m in re.finditer(rb"\x48\x8d\x05(....)\xc3", code, re.S):
        at = m.start() + 7 + struct.unpack("<i", m.group(1))[0]
        if not 0 <= at < len(code) or code[at:at + 9] != b"CAbility_":
            continue
        name = code[at:code.find(b"\0", at)].decode()
        ret = re.search(rb"\x31\xc0\xc3|\xb8(....)\xc3", code[m.end():m.end() + 0x140], re.S)
        if ret and name not in bits:
            bits[name] = 0 if ret.group(0)[0] == 0x31 else struct.unpack("<I", ret.group(1))[0]
    return bits


def pc_bits(exe: bytes) -> dict:
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    count, opt = struct.unpack_from("<H", exe, pe + 6)[0], struct.unpack_from("<H", exe, pe + 20)[0]
    base = struct.unpack_from("<Q", exe, pe + 48)[0]
    secs = {exe[o:o + 8].rstrip(b"\0"): struct.unpack_from("<III", exe, o + 12)
            for o in (pe + 24 + opt + 40 * i for i in range(count))}
    tva, trs, trp = secs[b".text"]
    rva, rrs, rrp = secs[b".rdata"]

    def code(r, n):
        return exe[trp + r - tva:trp + r - tva + n] if 0 <= r - tva < trs else b""

    def name_in(r):
        c = code(r, 0x40)
        for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d](....)", c, re.S):
            t = r + m.start() + 7 + struct.unpack("<i", m.group(1))[0]
            o = rrp + t - rva
            if 0 <= t - rva < rrs and exe[o:o + 9] == b"CAbility_":
                return exe[o:exe.find(b"\0", o)].decode()

    def bit(r):
        b = code(r, 6)
        if b[:3] in (b"\x33\xc0\xc3", b"\x31\xc0\xc3"):
            return 0
        if len(b) == 6 and b[0] == 0xB8 and b[5] == 0xC3:
            return struct.unpack_from("<I", b, 1)[0]

    bits = {}
    for k in range(0, rrs - 32, 8):
        name = name_in(struct.unpack_from("<Q", exe, rrp + k)[0] - base)
        if not name or name in bits:
            continue
        for j in (1, 2, 3):
            v = bit(struct.unpack_from("<Q", exe, rrp + k + 8 * j)[0] - base)
            if v is not None:
                bits[name] = v
                break
    return bits


def ability_bits(data_dir: str) -> dict:
    up = os.path.dirname(os.path.abspath(data_dir))
    for exe, read in (("eboot.bin", ps4_bits), ("TheCrew.exe", pc_bits)):
        path = os.path.join(up, exe)
        if os.path.exists(path):
            with open(path, "rb") as f:
                return read(f.read())
    return {}


def generate(data_dir: str) -> dict:
    data = load(data_dir)
    ms, rvs = missions(data)
    acts, fams = activities(data)
    return {"table": table(data), "reward_values": rvs, "missions": ms, "abilities": ability_bits(data_dir),
            "activities": acts, "families": fams}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir")
    ap.add_argument("-o", "--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "tcreborn", "games", "v1_21", "data",
                                                        "rewards.json"))
    args = ap.parse_args()
    out, dst = generate(args.data_dir), args.out
    with open(dst, "w") as f:
        json.dump(out, f)
    print(f"{len(out['missions'])} missions, {len(out['activities'])} activities -> {dst}")


if __name__ == "__main__":
    main()
