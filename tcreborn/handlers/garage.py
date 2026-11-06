# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import struct

from tcreborn import cars, paths, peers, summit
from tcreborn.handlers import Ctx, register
from tcreborn.player import set_bit


CONDITION = 0x11


@register(0x68B6EEC5, "CAR_REPORT")
def car_report(ctx: Ctx, body: bytes):
    p = ctx.player
    if len(body) >= 0x59:
        repair = body[-1] in (1, 4)
        model, spec = struct.unpack_from(">QQ", body, 0x1C if repair else 0x48)
        value = 100 if repair else body[0x58]
        garage = cars.entries(p)
        for m, cs in garage:
            for k, cfg in enumerate(cs):
                if m == model and cars.spec(cfg) == spec and cfg[CONDITION] != value:
                    cs[k] = cfg[:CONDITION] + bytes([value]) + cfg[CONDITION + 1:]
                    cars.store(p, garage)
                    p.save()
                    break
    return 0x67ED13A9, struct.pack(">BIIH", 1, 0, p.bucks, 0)


@register(0x1F86C435, "PEER_REGISTER")
def peer_register(ctx: Ctx, body: bytes):
    if peers.enabled():
        peers.register(body)
    return 0x56CEFAEE, struct.pack(">BI", 1, peers.SESSION_NUMBER)


TOKENS = {(8, 0x7938316): 0, (0x10, 0x7938315): 1, (0x20, 0x7938317): 2}
SPEC_REPLY_TAIL = bytes([1]) + struct.pack(">H", 0) + bytes([0, 0, 0, 3])


def _spec_purchase(p, body: bytes) -> bytes:
    c = cars.CATALOG
    currency = body[0x1C]
    (count,) = struct.unpack_from(">I", body, 0x1D + 16)
    if count == 0:
        return struct.pack(">q", -1) + cars.array(1, cars.EMPTY_CONFIG) + cars.part_list([0] * 38)
    model = struct.unpack_from(">Q", body, 0x1D + 20)[0]
    fail = struct.pack(">Q", model) + cars.array(1, cars.EMPTY_CONFIG) + cars.part_list([0] * 38)
    (n,) = struct.unpack_from(">I", body, 0x1D + 28 + 16)
    if c is None or n == 0:
        return fail
    variant, *parts = struct.unpack_from(">Q9Q", body, 0x1D + 28 + 20)
    garage = cars.entries(p)
    configs = next((cs for m, cs in garage if m == model), None)
    if not configs or variant not in c.variants:
        return fail
    price = cars.spec_price(c, garage, variant)
    owned = next((cfg for cfg in configs if cars.variant_of(cfg) == variant), None)
    if owned is not None and price is None:
        return fail
    if price is not None:
        token = TOKENS.get((currency, int(c.variants[variant]["spec"], 16)))
        if currency == 1 and p.bucks >= price:
            p.bucks -= price
        elif currency == 2 and p.spend_cc(int(price / cars.CC_RATE)):
            pass
        elif token is not None and summit.state(p)["tokens"][token] > 0:
            summit.state(p)["tokens"][token] -= 1
        else:
            return fail
    s = cars.slots(c, variant, parts[1], parts[3], parts[5])
    if owned is None:
        configs[:] = [cars.with_state(cfg, 0) for cfg in configs]
        configs.append(cars.config(c, variant, s, len(configs) + 1))
        garage.sort(key=lambda e: e[0] != model)
        cars.store(p, garage)
        owned = configs[-1]
    else:
        s = list(struct.unpack_from(">38H", owned, 0x23))
    return struct.pack(">Q", model) + cars.array(1, cars.with_state(owned, cars.BOUGHT)) + cars.part_list(s)


@register(0xF70D87D3, "SPEC_PURCHASE")
def spec_purchase(ctx: Ctx, body: bytes):
    p = ctx.player
    element = _spec_purchase(p, body)
    p.save()
    return 0x05E0DCFF, (struct.pack(">H3I", 0, p.bucks, p.cc, 0) + cars.array(1, element)
                        + SPEC_REPLY_TAIL)


def _objects(raw: bytes, o: int, end: int):
    while o + 16 <= end:
        (n,) = struct.unpack_from(">I", raw, o)
        yield raw[o + 16:o + n + 4]
        o += n + 4


def _mount_request(body: bytes):
    try:
        (n,) = struct.unpack_from(">I", body, 0x1C)
        for car in _objects(body, 0x20 + 12, 0x20 + n):
            model = struct.unpack_from(">Q", car)[0]
            for dress in _objects(car, 8 + 4 + 12, len(car)):
                spec = struct.unpack_from(">Q", dress)[0]
                (count,) = struct.unpack_from(">I", dress, 8 + 4 + 12)
                pairs = [struct.unpack_from(">HH", dress, 8 + 4 + 16 + 4 * k) for k in range(count)]
                return model, spec, pairs
    except struct.error:
        return None
    return None


def _owned(bits: bytes, x: int) -> bool:
    n = struct.unpack_from(">I", bits, 16)[0]
    return x >> 6 < n and struct.unpack_from(">Q", bits, 20 + 8 * (x >> 6))[0] >> (x & 63) & 1


@register(0x7DC1F4F0, "MOUNT_PARTS")
def mount_parts(ctx: Ctx, body: bytes):
    p = ctx.player
    req = _mount_request(body)
    if req is None or not req[2]:
        return 0x3BBE49A4, struct.pack(">BH", 1, 0)
    model, spec, pairs = req
    garage = cars.entries(p)
    configs = next((cs for m, cs in garage if m == model), [])
    k = next((i for i, cfg in enumerate(configs) if cars.spec(cfg) == spec), None)
    if k is None:
        return 0x3BBE49A4, struct.pack(">BH", 0, 0)
    cfg = bytearray(configs[k])
    slots = list(struct.unpack_from(">38H", cfg, 0x23))
    player_parts = cars.player_parts(garage, p.dlc.get(73, cars.array(0, b"")), cars.array(
        len(p.owned_items), struct.pack(f">{len(p.owned_items)}Q", *p.owned_items)))
    first, = struct.unpack_from(">I", cfg, 0x6F)
    own = bytes(cfg[0x6F + 4 + first:])
    changed = False
    for slot, part in dict(pairs).items():
        if slot >= 38:
            continue
        bits = own if slot < cars.OWNED_SLOTS else player_parts
        if part == 0xFFFF or _owned(bits, part):
            slots[slot] = part
            changed = True
    if not changed:
        return 0x3BBE49A4, struct.pack(">BH", 0, 0)
    struct.pack_into(">38H", cfg, 0x23, *slots)
    configs[k] = bytes(cfg)
    cars.store(p, garage)
    p.save()
    return 0x3BBE49A4, struct.pack(">BH", 1, 0)


CAR_DISCOUNT_PERK, CAR_DISCOUNT_STEP = 14, 3


@register(0xBBF8C44B, "CAR_PURCHASE")
def car_purchase(ctx: Ctx, body: bytes):
    body = body.ljust(0x25 + 80, b"\0")
    p, c = ctx.player, cars.CATALOG
    currency = body[0x24]
    variant, paint, _2, _3, part, _5, livery = struct.unpack_from(">7Q", body, 0x25)
    v = c.variants.get(variant) if c is not None else None
    if v is None:
        return 0x4239B014, (struct.pack(">HIIIq", 0xFFFF, p.bucks, p.cc, 0, -1) + cars.array(0, b"")
                            + cars.array(0, b""))
    model = int(v["model"], 16)
    price = c.prices.get(model, 0)
    s = cars.slots(c, variant, paint, part, livery)
    garage = cars.entries(p)
    configs = next((cs for m, cs in garage if m == model), None)
    cfg = cars.config(c, variant, s, len(configs) + 1 if configs else 1)
    ok = False
    if currency == 1:
        price = price * (100 - CAR_DISCOUNT_STEP * p.perks[CAR_DISCOUNT_PERK]) // 100
        if p.bucks >= price:
            p.bucks -= price
            ok = True
    elif currency == 2 and p.spend_cc(int(price / cars.CC_RATE)):
        ok = True
    if ok:
        if configs is None:
            configs = []
            garage.append((model, configs))
        configs[:] = [cars.with_state(x, 0) for x in configs]
        configs.append(cfg)
        cars.store(p, garage)
        p.save()
    return 0x4239B014, (struct.pack(">HIIIq", 0 if ok else 0xFFFF, p.bucks, p.cc, 0, model if ok else -1)
                        + cars.array(1, cars.with_state(cfg, cars.BOUGHT)) + cars.part_list(s))


NPC_PATH = paths.table("npc.json")
NPC = json.load(open(NPC_PATH))["items"] if os.path.exists(NPC_PATH) else None


def _npc_items(body: bytes, o: int):
    (n,) = struct.unpack_from(">I", body, o)
    (count,) = struct.unpack_from(">I", body, o + 16)
    return [struct.unpack_from(">QQQ", body, o + 20 + 0x19 * k) for k in range(count)], o + 4 + n


@register(0xF93D21DA, "NPC_PURCHASE")
def npc_purchase(ctx: Ctx, body: bytes):
    p = ctx.player
    try:
        first, end = _npc_items(body, 0x1C)
        second, _ = _npc_items(body, end)
    except struct.error:
        first, second = [], []
    c = cars.CATALOG
    models = {m for m, _ in cars.entries(p)}
    bought = False
    for currency, items in ((1, first), (2, second)):
        for item_id, a, b in items:
            item = NPC.get(f"{item_id:#x}") if NPC is not None else None
            if item is None or item["config"] or (item["model"] and int(item["model"], 16) not in models):
                continue
            if item["catalog"] == 2 and a not in models:
                continue
            price = item["price"] if currency == 1 else int(item["price"] / cars.CC_RATE)
            if currency == 1:
                if p.bucks < price:
                    continue
                p.bucks -= price
            elif not p.spend_cc(price):
                continue
            part = c.part(item_id) if c is not None else 0xFFFF
            if part != 0xFFFF:
                set_bit(p.owned_items, part)
            bought = True
    if bought:
        p.save()
    return 0x27A7C7FE, struct.pack(">HIII", 0, p.bucks, p.cc, 0)
