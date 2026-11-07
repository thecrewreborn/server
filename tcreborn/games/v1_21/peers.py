# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import math
import struct
import threading
import time
from dataclasses import dataclass

from tcreborn import config
from tcreborn.ubi import stable_id
from tcreborn.wire import T_ARRAY, blob, empty_array, frame

MAX_PEERS = 5
STALE_S = 120
NET_ENTITY = 0xC8337621
P_PEER = 0x62230DFA
P_ORBIS = 0x9A42C791
P_PUNCH = 0x0547E4F3
PLATFORM_BY_PROPERTY = {P_ORBIS: "ps4", P_PUNCH: "pc"}


SESSION_NUMBER = 1


@dataclass
class Peer:
    player_id: int
    info: bytes
    entity: bytes
    x: float
    y: float
    seen: float
    order: int = 0

    @property
    def key(self) -> int:
        return struct.unpack_from(">I", self.info, len(self.info) - 8)[0]

    @property
    def platform(self) -> str | None:
        return next((PLATFORM_BY_PROPERTY[t] for t, _ in entity_properties(self.entity) if t in PLATFORM_BY_PROPERTY),
                    None)

    @property
    def name(self) -> str:
        p = 4 + 4 + _u32(self.info, 4) + 8 + 4 + 4 + 2 + 2 + 1 + 1
        return self.info[p + 4:p + 4 + _u32(self.info, p)].decode("utf-16-le", "replace")

    def element(self, viewer: "Peer | None") -> bytes:
        crossing = viewer is not None and viewer.platform is not None and self.platform not in (None, viewer.platform)
        info = struct.pack(">I", SESSION_NUMBER) + self.info[4:]
        if crossing:
            info = info[:-8] + struct.pack(">I", viewer.key) + info[-4:]
        props, changed = [], False
        for t, payload in entity_properties(self.entity):
            if t == P_PEER:
                payload, changed = info, True
            elif crossing and t in PLATFORM_BY_PROPERTY:
                (t, payload), changed = platform_property(viewer.platform, self.name), True
            props.append((t, payload))
        return info + blob(net_entity(props) if changed else self.entity)


class Peers:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.by_id: dict[int, Peer] = {}
        self.lock = threading.Lock()
        self.count = 0

    def add(self, pid: int, info: bytes, entity: bytes, x: float, y: float) -> None:
        with self.lock:
            old = self.by_id.get(pid)
            if old and self.clock() - old.seen <= STALE_S:
                order = old.order
            else:
                self.count += 1
                order = self.count
            self.by_id[pid] = Peer(pid, info, entity, x, y, self.clock(), order)

    def forget(self, pid: int) -> None:
        with self.lock:
            self.by_id.pop(pid, None)

    def moved(self, pid: int, x: float, y: float) -> None:
        with self.lock:
            p = self.by_id.get(pid)
            if p:
                p.x, p.y, p.seen = x, y, self.clock()

    def others(self, pid: int, x: float, y: float) -> list[tuple[float, Peer]]:
        with self.lock:
            now = self.clock()
            near = [(math.hypot(p.x - x, p.y - y), p) for p in self.by_id.values()
                    if p.player_id != pid and now - p.seen <= STALE_S]
        return sorted(near, key=lambda e: e[0])[:MAX_PEERS]

    def hosts(self, pid: int) -> list[Peer]:
        with self.lock:
            now = self.clock()
            me = self.by_id.get(pid)
            mine = me.order if me else float("inf")
            older = [p for p in self.by_id.values()
                     if p.player_id != pid and now - p.seen <= STALE_S and p.order < mine]
        return sorted(older, key=lambda p: p.order)[:MAX_PEERS]


PEERS = Peers()


def entity_properties(entity: bytes) -> list[tuple[int, bytes]]:
    props, p = [], 12
    while p + 16 <= len(entity):
        n = _u32(entity, p)
        size, kind = struct.unpack_from(">II", entity, p + 4)
        props.append((kind, entity[p + 16:p + 4 + size]))
        p += 4 + n
    return props


def net_entity(props: list[tuple[int, bytes]]) -> bytes:
    return frame(NET_ENTITY, b"".join(blob(frame(t, payload)) for t, payload in props))


def platform_property(platform: str, name: str) -> tuple[int, bytes]:
    if platform == "pc":
        return P_PUNCH, bytes(8) + b"\x08" + blob(stable_id("profile", name).encode()) + bytes(28)
    return P_ORBIS, name.encode("ascii", "replace")[:16].ljust(44, b"\0")


def enabled() -> bool:
    return config.multiplayer()


def _u32(b: bytes, p: int) -> int:
    return struct.unpack_from(">I", b, p)[0]


def info_end(body: bytes, p: int) -> int:
    p += 4
    p += 4 + _u32(body, p)
    p += 8 + 4 + 4 + 2 + 2 + 1 + 1
    p += 4 + _u32(body, p)
    return p + 1 + 4 + 4


def session_join_reply(body: bytes) -> bytes:
    end = info_end(body, 0)
    me_info = body[:end]
    host_id = struct.unpack_from(">Q", body, end)[0]
    me_id = struct.unpack_from(">Q", me_info, 4 + 4 + _u32(me_info, 4))[0]
    me, host = PEERS.by_id.get(me_id), PEERS.by_id.get(host_id)
    if not enabled() or host is None:
        return b"\x00\x00" + me_info + blob(b"")
    PEERS.moved(me_id, me.x, me.y) if me else None
    return b"\x01\x00" + host.element(me)


def parse_register(body: bytes) -> tuple[int, bytes, bytes, float, float]:
    y, x = struct.unpack_from(">ff", body, 0)
    p = 12
    p += 4 + _u32(body, p)
    pid = struct.unpack_from(">Q", body, p)[0]
    p += 8 + 4 + 4 + 2 + 2 + 1 + 1
    p += 4 + _u32(body, p)
    p += 1 + 4 + 4
    info = body[8:p]
    n = _u32(body, p)
    return pid, info, body[p + 4:p + 4 + n], x, y


def register(body: bytes) -> int:
    pid, info, entity, x, y = parse_register(body)
    PEERS.add(pid, info, entity, x, y)
    return pid


def _array(elements: list[bytes]) -> bytes:
    return blob(frame(T_ARRAY, struct.pack(">I", len(elements)) + b"".join(elements)))


def session_reply(pid: int, x: float, y: float) -> bytes:
    if not enabled():
        return empty_array()
    PEERS.moved(pid, x, y)
    me = PEERS.by_id.get(pid)
    return _array([p.element(me) for p in PEERS.hosts(pid)])


def nearby_reply(pid: int, x: float, y: float) -> bytes:
    if not enabled():
        return empty_array()
    PEERS.moved(pid, x, y)
    me = PEERS.by_id.get(pid)
    return _array([struct.pack(">fBB", d, 0, 0) + p.element(me) for d, p in PEERS.others(pid, x, y)])
