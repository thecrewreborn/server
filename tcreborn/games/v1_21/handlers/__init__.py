# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import time
import zlib
from dataclasses import dataclass, field
from typing import Callable

from tcreborn.codec import encode_default
from tcreborn.games.v1_21.player import Player
from tcreborn.wire import frame


def player_id_for(user_name: str) -> int:
    return 0x9000000000000000 | zlib.crc32(user_name.encode())


@dataclass
class Ctx:
    player_name: str = "Player"
    player_id: int = player_id_for("Player")
    player: Player = field(default_factory=Player)
    now_us: Callable[[], int] = field(default=lambda: int(time.time() * 1_000_000))
    pc: bool = False
    crew_group: int = 0
    verified: bool = False

    def client_now_us(self) -> int:
        from tcreborn import config, ubi
        from tcreborn.games.v1_21 import summit
        calendar = self.pc or ubi.ACCOUNTS is not None or config.multiplayer()
        return summit.calendar_time_us(self.now_us()) if calendar else self.now_us()


Handler = Callable[[Ctx, bytes], tuple[int, bytes]]
HANDLERS: dict[int, Handler] = {}
NAMES: dict[int, str] = {}

_AUTO_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "auto_replies.json")
with open(_AUTO_FILE) as _f:
    AUTO: dict[int, tuple[int, list[str]]] = {
        int(req, 16): (int(e["reply"], 16), e["layout"]) for req, e in json.load(_f).items()}


def register(req_id: int, name: str):
    def deco(fn: Handler) -> Handler:
        HANDLERS[req_id] = fn
        NAMES[req_id] = name
        return fn
    return deco


def name_of(req_id: int) -> str:
    if req_id in NAMES:
        return NAMES[req_id]
    return f"AUTO_0x{req_id:08X}" if req_id in AUTO else f"0x{req_id:08x}"


NO_REPLY = b""


def handle(ctx: Ctx, req_id: int, body: bytes) -> bytes | None:
    if req_id in HANDLERS:
        ctx.player.clamp()
        result = HANDLERS[req_id](ctx, body)
        return NO_REPLY if result is None else frame(*result)
    if req_id in AUTO:
        reply_type, layout = AUTO[req_id]
        return frame(reply_type, encode_default(layout))
    return None


from tcreborn.games.v1_21.handlers import (crew, entity, garage, group, login, mission, notify, police,
                                           presence, progress, session, skill, stats, summit, ticket)
from tcreborn.games.v1_21.handlers import replies
