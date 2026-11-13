# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import struct

from tcreborn.games.v1_21 import cars, physparts, rewards
from tcreborn.games.v1_21.handlers import Ctx, register
from tcreborn.games.v1_21.player import ALL_ABILITIES
from tcreborn.wire import T_ARRAY, blob, empty_array, frame, u64_array


def finish_mission(p, mission: int, score: int, now_us: int, paid: bool = True) -> None:
    p.complete_mission(mission)
    if rewards.TABLES is not None:
        if paid:
            xp, bucks = rewards.mission_reward(rewards.TABLES, mission, score, p.level)
            rewards.apply(rewards.TABLES, p, *rewards.allow(p, now_us, xp, bucks))
        earned = rewards.abilities(rewards.TABLES, p.missions_done)
        p.abilities = list(ALL_ABILITIES) if p.free_roam else earned


def is_dealer(mission: int) -> bool:
    return cars.CATALOG is not None and mission == cars.CATALOG.dealer


def pays_dealer(p, mission: int) -> bool:
    return is_dealer(mission) and mission not in p.missions_done


def dealer_car(p, body: bytes) -> bytes | None:
    c = cars.CATALOG
    variant = struct.unpack_from(">Q", body, 0x3A)[0]
    if c is None or variant == cars.NONE:
        return None
    v = c.variants.get(variant)
    model = int(v["model"], 16) if v else cars.NONE
    bucks = max(0, c.budget - c.prices.get(model, 0))
    if rewards.TABLES is not None:
        rewards.apply(rewards.TABLES, p, 0, bucks)
    else:
        p.bucks += bucks
    if v is None:
        return struct.pack(">q", -1) + empty_array() + cars.part_list([0] * 38) + struct.pack(">H", 0)
    a, b, d = (struct.unpack_from(">Q", body, o)[0] for o in (0x52, 0x62, 0x72))
    s = cars.slots(c, variant, a, b, d)
    garage = cars.entries(p)
    configs = next((cs for m, cs in garage if m == model), None)
    if configs is None:
        configs = []
        garage.append((model, configs))
    configs.append(cars.config(c, variant, s, cars.STORED))
    cars.store(p, garage)
    shown = configs[:-1] + [cars.with_state(configs[-1], cars.NEW)]
    return (struct.pack(">Q", model) + cars.array(len(shown), b"".join(shown)) + cars.part_list(s)
            + struct.pack(">H", 0))


def owned_dealer_car(p, body: bytes) -> bytes | None:
    c = cars.CATALOG
    variant = struct.unpack_from(">Q", body, 0x3A)[0]
    v = c.variants.get(variant) if c is not None else None
    if v is None:
        return None
    model = int(v["model"], 16)
    configs = next((cs for m, cs in cars.entries(p) if m == model), None)
    if not configs:
        return None
    k = next((i for i, cfg in enumerate(configs) if cars.variant_of(cfg) == variant), len(configs) - 1)
    shown = [cars.with_state(cfg, cars.NEW) if i == k else cfg for i, cfg in enumerate(configs)]
    s = list(struct.unpack_from(">38H", configs[k], 0x23))
    return (struct.pack(">Q", model) + cars.array(len(shown), b"".join(shown)) + cars.part_list(s)
            + struct.pack(">H", 0))


def reward_part(p, mission: int, score: int, body: bytes) -> int | None:
    t = rewards.TABLES
    m = t.missions.get(mission) if t is not None else None
    if m is None or len(m.get("thresholds", [])) < 4 or len(body) < 0x38:
        return None
    medal = rewards._medal(t, m["thresholds"], score, p.level) if score >= m["thresholds"][0] else -1
    model, spec = struct.unpack_from(">QQ", body, 0x28)
    return physparts.mission_part(p, mission, medal, model, spec)


@register(0xBB6835CF, "MISSION_END")
def mission_end(ctx: Ctx, body: bytes):
    mission, score = struct.unpack_from(">QI", body, 0x1C)
    p = ctx.player
    dealer = pays_dealer(p, mission)
    finish_mission(p, mission, score, ctx.now_us())
    part = reward_part(p, mission, score, body)
    car = dealer_car(p, body) if dealer else owned_dealer_car(p, body) if is_dealer(mission) else None
    p.save()
    if car is None:
        car = struct.pack(">q", -1) + 2 * empty_array() + struct.pack(">H", 0x20)
    granted = u64_array([physparts.TABLE["ids"][part]]) if part is not None and physparts.ANNOUNCE_PART \
        else empty_array()
    return 0x02459285, (struct.pack(">BQ5I", 1, mission, p.level, p.xp, p.bucks, p.cc, p.perk_points)
                        + granted + u64_array(p.abilities) + car + empty_array() + struct.pack(">I", 0))


@register(0xC43C7F34, "PROGRESS_BIT")
def progress_bit(ctx: Ctx, body: bytes):


    (id_,) = struct.unpack_from(">H", body, 0x1C)
    ctx.player.set_progress_bit(id_)
    ctx.player.save()
    return 0xCD246F74, b"\x01" + u64_array(ctx.player.progress) + b"\x00\x00"


RATING_EMPTY = bytes(29)


def _rating(player_id: int) -> bytes:


    return struct.pack(">Q", player_id) + bytes.fromhex("4041f07c41055555ffffffff100000000000000000")


@register(0xE20F5542, "PLAYER_RATINGS")
def player_ratings(ctx: Ctx, body: bytes):

    (count,) = struct.unpack_from(">I", body, 28 + 4 + 12)
    ids = struct.unpack_from(f">{count}Q", body, 28 + 4 + 16)
    per_player = [blob(frame(T_ARRAY, struct.pack(">I", 4) + 2 * RATING_EMPTY + 2 * _rating(pid)))
                  for pid in ids if pid == ctx.player_id]
    return 0x752FDA89, b"\x01" + blob(frame(T_ARRAY, struct.pack(">I", len(per_player)) + b"".join(per_player)))
