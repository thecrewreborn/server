# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import struct

from tcreborn.games.v1_21 import tables
from tcreborn.games.v1_21 import cars, rewards
from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.games.v1_21.player import PERK_MAX, PERKS, set_bit
from tcreborn.wire import blob, frame, serializer_list, u64_array

PERK_POINT_BUCKS, PERK_POINT_CREDITS = 100000, 2000
RESET_PER_LEVEL = 100
FIRST_BUCKS, FIRST_XP = 2000, 25


def _add_bucks(p, d: int) -> None:
    if rewards.TABLES is not None:
        rewards.add_bucks(rewards.TABLES, p, d)
    else:
        p.bucks = max(0, p.bucks + d)


def _add_xp(p, d: int) -> None:
    if rewards.TABLES is not None:
        rewards.add_xp(rewards.TABLES, p, d)
    else:
        p.xp += d


@register(0xBF9B8FBD, "PERKS")
def perks(ctx: Ctx, body: bytes):
    body = body.ljust(0x3F, b"\0")
    p = ctx.player
    for _ in range(body[0x20]):
        if p.bucks >= PERK_POINT_BUCKS:
            p.bucks -= PERK_POINT_BUCKS
            p.perk_points += 1
    for _ in range(body[0x1E]):
        if p.spend_cc(PERK_POINT_CREDITS):
            p.perk_points += 1
    ok = True
    if body[0x1C] == 1:
        cost = RESET_PER_LEVEL * sum(p.perks)
        if p.bucks >= cost:
            p.bucks -= cost
            p.perks = [0] * PERKS
        else:
            ok = False
    else:
        p.perks = [min(x, PERK_MAX) for x in body[0x21:0x21 + PERKS].ljust(PERKS, b"\0")]
        if sum(p.perks) > p.perk_points:
            p.perks, ok = [0] * PERKS, False
    p.save()
    return 0xB37AA94C, struct.pack(">BIIHH", ok, p.bucks, p.cc, p.perk_points, 0)


@register(0xE6D82939, "FIRST_REWARD")
def first_reward(ctx: Ctx, body: bytes):
    body = body.ljust(0x3F, b"\0")
    p = ctx.player
    if set_bit(p.rewarded, body[0x1D]):
        _add_bucks(p, FIRST_BUCKS)
        _add_xp(p, FIRST_XP)
        p.save()
    return 0x884B40AE, (b"\x01" + u64_array(p.rewarded)
                        + struct.pack(">BIIHH", p.level, p.xp, p.bucks, p.perk_points, 0))


@register(0x7C044567, "TABLE_BIT")
def table_bit(ctx: Ctx, body: bytes):
    body = body.ljust(0x3F, b"\0")
    p = ctx.player
    if set_bit(p.table_bits, struct.unpack_from(">H", body, 0x1C)[0]):
        p.save()
    return 0xA7B8025B, b"\x01" + u64_array(p.table_bits) + b"\x00\x00"


T_COLLECTIBLE = 0x9234FC36


MAX_COLLECTIBLES = 4096


SETS_PATH = tables.path("collectibles.json")
SETS = json.load(open(SETS_PATH)) if os.path.exists(SETS_PATH) else None


def complete_set(p, item: int) -> None:
    if SETS is None or cars.CATALOG is None:
        return
    group = SETS["set_of"].get(f"{item:#x}")
    if group is None:
        return
    done = sum(1 for i, v in p.collectibles if SETS["set_of"].get(f"{i:#x}") == group
               and struct.unpack(">f", struct.pack(">I", v))[0] >= 1.0)
    part = cars.CATALOG.part(int(group, 16))
    if done >= SETS["needed"][group] and part != 0xFFFF:
        set_bit(p.owned_items, part)


@register(0x740437D6, "COLLECTIBLES")
def collectibles(ctx: Ctx, body: bytes):
    p = ctx.player
    try:
        (count,) = struct.unpack_from(">I", body, 8 + 16)
        count = min(count, max(0, (len(body) - 8 - 20) // 12))
        entries = [struct.unpack_from(">QI", body, 8 + 20 + 12 * k) for k in range(count)]
    except struct.error:
        entries = []
    known = {i: k for k, (i, _v) in enumerate(p.collectibles)}
    for item, value in entries:
        if item in known:
            p.collectibles[known[item]][1] = value
        elif len(p.collectibles) < MAX_COLLECTIBLES:
            known[item] = len(p.collectibles)
            p.collectibles.append([item, value])
    groups = set()
    for item, _value in entries:
        group = SETS["set_of"].get(f"{item:#x}") if SETS is not None else None
        if group is not None and group not in groups:
            groups.add(group)
            complete_set(p, item)
    if entries:
        p.save()
    return 0x48A252AB, b"\x01"


@register(0x2FC9E8A6, "COLLECTIBLE_LIST")
def collectible_list(ctx: Ctx, body: bytes):
    items = [frame(T_COLLECTIBLE, struct.pack(">QI", i, v)) for i, v in ctx.player.collectibles]
    return 0x573316DD, struct.pack(">Q", ctx.player_id) + blob(serializer_list(items))


REGIONS_PATH = tables.path("regions.json")
REGIONS = json.load(open(REGIONS_PATH)) if os.path.exists(REGIONS_PATH) else None
REWARD_STATE = 5


@register(0x88648CE9, "REGION_REWARD")
def region_reward(ctx: Ctx, body: bytes):
    body = body.ljust(0x1D, b"\0")
    p, c = ctx.player, cars.CATALOG
    idx = body[0x1C]
    if REGIONS is not None and c is not None and idx < len(REGIONS["regions"]):
        key = REGIONS["regions"][idx]
        have = sum(1 for n, k in enumerate(REGIONS["bits"]) if k == key
                   and n >> 6 < len(p.table_bits) and p.table_bits[n >> 6] >> (n & 63) & 1)
        variant = int(REGIONS["cars"][key], 16)
        garage = cars.entries(p)
        owned = any(cars.variant_of(cfg) == variant for _m, cs in garage for cfg in cs)
        if have >= REGIONS["threshold"] and variant in c.variants and not owned:
            model = int(c.variants[variant]["model"], 16)
            s = cars.slots(c, variant, cars.NONE, cars.NONE, cars.NONE)
            configs = next((cs for m, cs in garage if m == model), None)
            if configs is None:
                configs = []
                garage.append((model, configs))
            cfg = cars.config(c, variant, s, cars.STORED)
            configs.append(cfg)
            cars.store(p, garage)
            p.save()
            return 0x6939A37D, (struct.pack(">HQ", 0, model) + cars.array(1, cars.with_state(cfg, REWARD_STATE))
                                + cars.part_list(s))
    from tcreborn.games.v1_21.handlers.replies import TEMPLATES
    return 0x6939A37D, bytes.fromhex(TEMPLATES["0x88648ce9"]["body"])
