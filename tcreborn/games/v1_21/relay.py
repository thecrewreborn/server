# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import collections
import datetime
import json
import os
import re
import socket
import struct
import threading
import time
import traceback
from dataclasses import dataclass

from tcreborn.games.v1_21.crypto import IV0, K0, decrypt, encrypt
from tcreborn.discovery import local_ip_for, no_connreset
from tcreborn.limits import LineLimit, RateLimit
from tcreborn import config, paths, ubi
from tcreborn.saves import DEFAULT_NAME, account_key, save_name
from tcreborn.games.v1_21 import peers
from tcreborn.games.v1_21.handlers import NO_REPLY, Ctx, handle, name_of, player_id_for
from tcreborn.games.v1_21.player import ALL_ABILITIES, Player, dlc_fields, unlock_fields
from tcreborn.games.v1_21.handlers.replies import SERVICE_BY_ADDR
from tcreborn.session import STORE_NAME, Session, SessionStore
from tcreborn.wire import (DIRECT_HEADER, REPLY_FLAGS, T_ACK, T_ACK_CONFIRM, T_DATA, T_RESOLVE, ack, ack_confirm,
                        assemble, data_chunks, parse_message, release_all, resolve_reply)

Packet = tuple[bytes, tuple[str, int]]
NO_ACK_CHANNELS = {0x49466F25, 0x5ABA06B7}
GET_PLAYER = 0x589F57FB

_TICKET = re.compile(rb"b2ZmbGluZS10aWNrZXQ6[A-Za-z0-9+/=]*")
_KEYS = ("session_key", "session_iv")

GUEST_IDLE_S = 120
NAMED_IDLE_S = 15 * 60
MAX_GUESTS = 2000
MAX_GUESTS_PER_IP = 32
REPLY_CACHE = 256
REPLY_BYTES = 4 * 2 ** 20


GUEST_REPLY_CACHE = 32
GUEST_REPLY_BYTES = 64 * 1024
REPLY_BYTES_TOTAL = 256 * 2 ** 20


REQUESTS_PER_S = 100
REQUEST_BURST = 500


REPLY_BYTES_PER_S = 2 ** 20
REPLY_BYTES_BURST = 16 * 2 ** 20
BUDGET_ADDRESSES = 10_000
BUDGET_IDLE_S = 600
HANDSHAKES_PER_S = 10
PLAYER_IDLE_S = 30 * 60


PLAINTEXT_REQUESTS = {0x997BED03, 0xEA3A66D7, 0x9101DE70, 0x368D58C9}


GUEST_REQUESTS = frozenset({
    0x036D7773,
    0x27C711C5,
    0x368D58C9,
    0x40BD5E2B,
    0x720CC96E,
    0x9AA22640,
    0xA1155286,
    0xB1F54E5C,
    0xCD3B60F2,
    0xD14A2746,
})
MAX_UNKNOWN = 1000


SAY_WINDOW_S = 60
SAY_UNVERIFIED_PER_MIN = 20


LOG_UNVERIFIED_PER_MIN = 2000
_clock = time.monotonic


def hide_tickets(data: bytes) -> bytes:
    return _TICKET.sub(lambda m: b"*" * len(m.group()), data)


def shown_hex(data: bytes) -> str:
    full = hide_tickets(data).hex()
    return full if len(full) <= ubi.LOG_HEX else f"{full[:ubi.LOG_HEX]}... <{len(data)} bytes>"


def player_name(get_player_body: bytes) -> str | None:
    if len(get_player_body) < 0x30:
        return None
    n = struct.unpack_from(">I", get_player_body, 0x2C)[0]
    raw = get_player_body[0x30:0x30 + n]
    return raw.decode("utf-8", "replace") if 0 < n <= 64 and len(raw) == n else None


def player_ticket(get_player_body: bytes) -> str | None:
    if len(get_player_body) < 0x30:
        return None
    n = struct.unpack_from(">I", get_player_body, 0x2C)[0]
    at = 0x30 + n
    if not 0 < n <= 64 or len(get_player_body) < at + 5:
        return None
    size = struct.unpack_from(">I", get_player_body, at + 1)[0]
    raw = get_player_body[at + 5:at + 5 + size]
    return raw.decode("ascii") if 0 < size <= 128 and len(raw) == size and raw.isascii() else None


def _keep_newest(table: dict, key, value) -> None:
    table[key] = value
    while len(table) > REPLY_CACHE:
        del table[next(iter(table))]


def _store_reply(sess: Session, key, chunks: list[bytes], count: int | None = None, size: int | None = None,
                 keep_oversized: bool = True) -> None:
    count = REPLY_CACHE if count is None else count
    size = REPLY_BYTES if size is None else size
    old = sess.replies.pop(key, None)
    if old is not None:
        sess.reply_bytes -= sum(map(len, old))
    if not keep_oversized and sum(map(len, chunks)) > size:
        return
    sess.replies[key] = chunks
    sess.reply_bytes += sum(map(len, chunks))
    while len(sess.replies) > 1 and (len(sess.replies) > count or sess.reply_bytes > size):
        sess.reply_bytes -= sum(map(len, sess.replies.pop(next(iter(sess.replies)))))


@dataclass
class Budget:
    tokens: float
    reply_bytes: float
    at: float


def _ts() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def _addr_bytes(ip: str, port: int) -> bytes:
    return socket.inet_aton(ip) + struct.pack(">H", port)


def start_crew_credits(player: Player, new: bool) -> None:
    if config.crew_credits_refill():
        player.cc_fixed = config.crew_credits()
    else:
        player.cc_fixed = 0
        if new:
            player.crew_credits = config.crew_credits()


class Server:
    guest_requests = GUEST_REQUESTS

    def __init__(self, ip: str, port: int, log_path: str | None, save_path: str | None = None,
                 sender=None, abilities: str = "none",
                 unlocks: str = "none", dlc: str = "all",
                 proxy_key: tuple[bytes, bytes] = (K0, IV0)):
        self.ip, self.port, self.log_path = ip, port, log_path
        self.proxy_key = proxy_key
        self.sessions: dict[tuple[str, int], Session] = {}

        self.store = SessionStore(os.path.join(os.path.dirname(os.path.abspath(save_path)), STORE_NAME)) \
            if save_path else None
        self.unknown: set[tuple[str, int]] = set()

        self.guests: collections.OrderedDict[tuple[str, int], None] = collections.OrderedDict()
        self.guests_by_ip: dict[str, collections.OrderedDict[tuple[str, int], None]] = {}


        self.unproven: collections.OrderedDict[tuple[str, int], None] = collections.OrderedDict()
        self.handshakes = RateLimit(HANDSHAKES_PER_S, 1.0)

        self.budgets: collections.OrderedDict[str, Budget] = collections.OrderedDict()
        self.proven: dict[tuple[str, int], Session] = {}


        self.guest_caches: collections.OrderedDict[tuple[str, int], None] = collections.OrderedDict()
        self.player_caches: collections.OrderedDict[tuple[str, int], None] = collections.OrderedDict()
        self.reply_total = 0
        self.stored: set[tuple[str, int]] = set()
        self.swept = 0.0
        self.direct_partial: dict[tuple[str, int], dict] = {}
        self.muted = False


        self.said: collections.OrderedDict[tuple[str, int], float] = collections.OrderedDict()
        self.say_window, self.say_count, self.say_left_out = _clock(), 0, 0
        self.say_lock = threading.Lock()
        self.guest_lines = LineLimit(f"relay port {port}: requests before a verified GET_PLAYER")
        self.evicted_lines = LineLimit(f"relay port {port}: resends of replies no longer cached")
        self.current: tuple[str, int] | None = None

        self.log_window, self.log_count, self.log_left_out = _clock(), 0, 0
        self.save_path, self.abilities, self.unlocks, self.dlc = save_path, abilities, unlocks, dlc

        self.players: dict[str, Ctx] = {}
        self.player_seen: dict[str, float] = {}


        self.ctx = self._new_ctx(DEFAULT_NAME, None)
        self.sender = sender
        self._restore()

    def _restore(self) -> None:
        restored = self.store.load() if self.store else {}

        restored = {addr: sess for addr, sess in restored.items() if sess.name}
        self.stored = set(restored)
        for addr, sess in restored.items():
            sess.seen = _clock()
            self.sessions[addr] = sess
        if restored:
            print(f"[{_ts()}] relay port {self.port}: {len(restored)} game session(s) from before the restart kept",
                  flush=True)

    def _remember(self, sess: Session | None, addr: tuple[str, int]) -> None:
        if not (self.store and sess is not None):
            return
        sess.stamp = time.time()
        if sess.name or addr in self.stored:

            self._store_named()

    def _new_session(self, addr: tuple[str, int]) -> Session:
        sess = Session()
        sess.seen = _clock()
        self._drop(addr)
        mine = self.guests_by_ip.get(addr[0], {})
        while len(mine) >= MAX_GUESTS_PER_IP:

            self._drop(next((a for a in mine if a in self.unproven), None) or next(iter(mine)))
        while len(self.guests) >= MAX_GUESTS:
            self._drop(next(iter(self.unproven)) if self.unproven else next(iter(self.guests)))
        self.sessions[addr] = sess
        self._guest_active(addr)
        return sess

    def _guest_active(self, addr: tuple[str, int]) -> None:
        self.guests[addr] = None
        self.guests.move_to_end(addr)
        mine = self.guests_by_ip.setdefault(addr[0], collections.OrderedDict())
        mine[addr] = None
        mine.move_to_end(addr)
        sess = self.sessions.get(addr)
        if sess is None or self.proven.get(addr) is not sess:
            self.unproven[addr] = None
            self.unproven.move_to_end(addr)

    def _unguest(self, addr: tuple[str, int]) -> None:
        self.guests.pop(addr, None)
        self.unproven.pop(addr, None)
        mine = self.guests_by_ip.get(addr[0])
        if mine is not None:
            mine.pop(addr, None)
            if not mine:
                del self.guests_by_ip[addr[0]]

    def _expire(self, now: float) -> None:
        if now - self.swept < 1:
            return
        self.swept = now
        in_use = {id(s.ctx) for s in self.sessions.values() if s.ctx is not None}
        idle = [a for a, s in self.sessions.items() if now - s.seen > (NAMED_IDLE_S if s.name else GUEST_IDLE_S)]
        named = any(self.sessions[a].name for a in idle)
        for addr in idle + [a for a in self.guests if a not in self.sessions]:
            self._drop(addr)
        if named:
            self._store_named()
        for key, ctx in list(self.players.items()):
            if id(ctx) in in_use:
                self.player_seen[key] = now
            elif now - self.player_seen.setdefault(key, now) > PLAYER_IDLE_S:
                del self.players[key]
                self.player_seen.pop(key, None)

    def _drop(self, addr: tuple[str, int]) -> None:
        self._unguest(addr)
        self._forget_session(addr)
        pending = self.direct_partial.pop(addr, None)
        if pending:
            release_all(pending)
        self.proven.pop(addr, None)

    def _forget_session(self, addr: tuple[str, int]) -> None:
        old = self.sessions.pop(addr, None)
        if old is not None and old.partial:
            release_all(old.partial)
        self.guest_caches.pop(addr, None)
        self.player_caches.pop(addr, None)
        if old is not None:
            self.reply_total -= old.reply_bytes

    def _cache_reply(self, sess: Session, addr: tuple[str, int], key, chunks: list[bytes]) -> None:
        before = sess.reply_bytes
        if sess.name:
            _store_reply(sess, key, chunks)
        else:
            _store_reply(sess, key, chunks, GUEST_REPLY_CACHE, GUEST_REPLY_BYTES, keep_oversized=False)
        self.reply_total += sess.reply_bytes - before
        mine, others = (self.player_caches, self.guest_caches) if sess.name else (self.guest_caches, self.player_caches)
        others.pop(addr, None)
        if sess.replies:
            mine[addr] = None
            mine.move_to_end(addr)
        else:
            mine.pop(addr, None)
        while self.reply_total > REPLY_BYTES_TOTAL:
            table = next((t for t in (self.guest_caches, self.player_caches) if any(a != addr for a in t)), None)
            if table is None:
                break
            oldest = next(a for a in table if a != addr)
            del table[oldest]
            other = self.sessions.get(oldest)
            if other is not None:
                self.reply_total -= other.reply_bytes
                other.replies.clear()
                other.reply_bytes = 0

    def _store_named(self) -> None:
        if self.store:
            kept = {a: s for a, s in self.sessions.items() if s.name}
            self.store.save(kept)
            self.stored = set(kept)

    def _new_ctx(self, name: str, path: str | None) -> Ctx:
        new = not (path and os.path.exists(path))
        ctx = Ctx(player_name=name, player_id=player_id_for(name), player=Player.load(path))
        if self.abilities == "all" and not ctx.player.abilities:
            ctx.player.abilities = list(ALL_ABILITIES)
            ctx.player.save()
        ctx.player.unlocks = unlock_fields(self.unlocks)
        ctx.player.dlc = dlc_fields(self.dlc)
        start_crew_credits(ctx.player, new)
        return ctx

    def ctx_for_name(self, name: str | None) -> Ctx:
        if not name or name == DEFAULT_NAME or not self.save_path:
            return self.ctx
        key = account_key(name)
        if key not in self.players:
            folder = os.path.join(os.path.dirname(os.path.abspath(self.save_path)), "players")
            path = os.path.join(folder, save_name(name) + ".json")
            self.players[key] = self._new_ctx(name, path)
        self.player_seen[key] = _clock()
        return self.players[key]

    def ctx_for(self, addr: tuple[str, int], sess: Session | None = None) -> Ctx:
        if sess is None:
            sess = self.sessions.get(addr)
        if sess is not None and sess.ctx is not None:
            return sess.ctx
        if sess is None or not sess.name:


            ctx = self._new_ctx(DEFAULT_NAME, None)
            if sess is not None:
                sess.ctx = ctx
            return ctx
        ctx = self.ctx_for_name(sess.name)
        ctx.verified, ctx.client_ip = True, addr[0]
        ctx.pc = (sess.platform or ubi.PLATFORMS.get(addr[0])) == "uplay"
        sess.ctx = ctx
        return ctx

    def public_ip(self, peer: tuple[str, int]) -> str:
        if advertised := config.advertised_ip():
            return advertised
        return local_ip_for(peer[0]) if self.ip == "0.0.0.0" else self.ip

    def log(self, entry: dict) -> None:
        if not self.log_path or self.muted:
            return
        if not self._may_log():
            return


        entry = {k: v for k, v in entry.items() if k not in _KEYS and not (k == "hex" and entry.get("step") == 2)}
        for k in ("hex", "plain", "payload"):
            if isinstance(entry.get(k), str):
                full = hide_tickets(bytes.fromhex(entry[k])).hex()
                entry[k] = full[:ubi.LOG_HEX]
                if len(full) > ubi.LOG_HEX:
                    entry[k + "_bytes"] = len(full) // 2
        entry["time"] = datetime.datetime.now().isoformat()
        try:
            ubi.append_log(self.log_path, json.dumps(entry) + "\n")
        except OSError as e:
            ubi.log_failed(self.log_path, e)

    def _may_log(self) -> bool:
        sess = self.sessions.get(self.current) if self.current else None
        if sess is not None and sess.name:
            return True
        now = _clock()
        if now - self.log_window >= SAY_WINDOW_S:
            if self.log_left_out:
                try:
                    ubi.append_log(self.log_path, json.dumps({"kind": "suppressed", "entries": self.log_left_out,
                                                              "time": datetime.datetime.now().isoformat()}) + "\n")
                except OSError as e:
                    ubi.log_failed(self.log_path, e)
            self.log_window, self.log_count, self.log_left_out = now, 0, 0
        if self.log_count >= LOG_UNVERIFIED_PER_MIN:
            self.log_left_out += 1
            return False
        self.log_count += 1
        return True

    def _guest_dropped(self, addr: tuple[str, int], text: str) -> None:
        if self.guest_lines.allow(addr[0], _clock()):
            self.say(addr, text, always=True)

    def _evicted_dropped(self, addr: tuple[str, int], text: str) -> None:
        if self.evicted_lines.allow(addr[0], _clock()):
            self.say(addr, text, always=True)

    def say(self, addr: tuple[str, int], text: str, always: bool = False) -> None:
        if not always and not self._may_say(addr):
            return
        print(f"[{_ts()}] {addr[0]}:{addr[1]} {text}", flush=True)

    def _may_say(self, addr: tuple[str, int]) -> bool:
        with self.say_lock:
            return self._may_say_locked(addr)

    def _may_say_locked(self, addr: tuple[str, int]) -> bool:
        now = _clock()
        if now - self.say_window >= SAY_WINDOW_S:
            if self.say_left_out:
                print(f"[{_ts()}] relay port {self.port}: {self.say_left_out} line(s) from unverified senders "
                      f"suppressed", flush=True)
            self.say_window, self.say_count, self.say_left_out = now, 0, 0
        sess = self.sessions.get(addr)
        if sess is not None and sess.name:
            return True
        while self.said and now - next(iter(self.said.values())) >= SAY_WINDOW_S:
            self.said.popitem(last=False)
        if addr in self.said or self.say_count >= SAY_UNVERIFIED_PER_MIN:
            self.say_left_out += 1
            return False
        self.said[addr] = now
        self.say_count += 1
        return True

    def handle_datagram(self, data: bytes, addr: tuple[str, int]) -> list[Packet]:
        if addr == (self.ip, self.port):
            return []
        header, body = data[:6], data[6:]
        peer = f"{addr[0]}:{addr[1]}"
        known = self.sessions.get(addr)
        self.current = addr

        self.muted = not (known and known.connected)
        now = _clock()
        self._expire(now)
        if len(data) == 6 and header[4:6] in (b"\x00\x00", b"\x00\x01") and not self.handshakes.allow(addr[0], now):
            return []
        if addr in self.guests:
            self._guest_active(addr)
        self.log({"dir": "in", "src": peer, "hex": data.hex()})
        if len(data) == 6 and header[4:6] == b"\x00\x00":
            return self._probe(addr)
        if len(data) == 6 and header[4:6] == b"\x00\x01":
            out = self._key_exchange(addr)
            self.unknown.discard(addr)
            self._remember(self.sessions.get(addr), addr)
            return out
        if header == DIRECT_HEADER:
            return self._direct(header, body, addr)
        sess = self.sessions.get(addr)
        if not (sess and sess.connected and body):
            self.log({"dir": "in", "src": peer, "kind": "unknown"})
            if body and addr not in self.unknown:
                if len(self.unknown) >= MAX_UNKNOWN:
                    self.unknown.clear()
                self.unknown.add(addr)
                self.say(addr, "packets of a game session this server does not know (no key): dropped until the "
                               "game connects again")
            return []
        sess.seen = _clock()
        for caches in (self.guest_caches, self.player_caches):
            if addr in caches:
                caches.move_to_end(addr)
        if sess.restored:
            sess.restored = False
            self.say(addr, f"game session from before the restart goes on"
                           + (f" (player {sess.name})" if sess.name else ""))
        return self._tunnel(sess, header, body, addr)

    def _probe(self, addr) -> list[Packet]:
        self._new_session(addr)
        plain = _addr_bytes(self.public_ip(addr), self.port) + _addr_bytes(*addr) + struct.pack(">II", 1, 1)
        reply = b"\x00" * 6 + encrypt(*self.proxy_key, plain)
        self.say(addr, f"probe -> handshake 1 ({len(reply)}B)")
        self.log({"dir": "out", "dst": f"{addr[0]}:{addr[1]}", "step": 1, "hex": reply.hex()})
        return [(reply, addr)]

    def _key_exchange(self, addr) -> list[Packet]:

        sess = self._new_session(addr)
        sess.key, sess.iv, sess.connected = os.urandom(32), os.urandom(16), True
        self.muted = False
        reply = b"\x00\x00\x00\x00\x00\x01" + encrypt(*self.proxy_key, sess.key + sess.iv)
        self.say(addr, f"key request -> handshake 2 ({len(reply)}B)")
        self.log({"dir": "out", "dst": f"{addr[0]}:{addr[1]}", "step": 2, "hex": reply.hex(),
                  "session_key": sess.key.hex(), "session_iv": sess.iv.hex()})
        return [(reply, addr)]

    def budget(self, ip: str) -> Budget:
        now = _clock()
        b = self.budgets.get(ip)
        if b is None:
            b = self.budgets[ip] = Budget(float(REQUEST_BURST), float(REPLY_BYTES_BURST), now)
        else:
            gone = max(0.0, now - b.at)
            b.tokens = min(float(REQUEST_BURST), b.tokens + gone * REQUESTS_PER_S)
            b.reply_bytes = min(float(REPLY_BYTES_BURST), b.reply_bytes + gone * REPLY_BYTES_PER_S)
            b.at = now
        self.budgets.move_to_end(ip)
        while len(self.budgets) > BUDGET_ADDRESSES or now - next(iter(self.budgets.values())).at > BUDGET_IDLE_S:
            self.budgets.popitem(last=False)
        return b

    def _may_request(self, addr: tuple[str, int]) -> bool:
        b = self.budget(addr[0])
        if b.tokens < 1:
            return False
        b.tokens -= 1
        return True

    def _bytes_ok(self, addr: tuple[str, int]) -> bool:
        return self.budget(addr[0]).reply_bytes >= 0

    def _charge_bytes(self, addr: tuple[str, int], n: int) -> None:
        if n:
            self.budget(addr[0]).reply_bytes -= n

    def _ack(self, msg, received: int = 1, ctx: Ctx | None = None) -> list[bytes]:
        if msg.channel in NO_ACK_CHANNELS:
            return []
        return [ack(msg.transfer, msg.channel, msg.flags, (ctx or self.ctx).client_now_us(), received)]

    def _reply_chunks(self, msg, rpc: int, request: bytes, ctx: Ctx | None = None) -> list[bytes] | None:
        try:
            inner = handle(ctx or self.ctx, rpc, request[12:])
        except (struct.error, ValueError, IndexError) as e:
            self.log({"kind": "handler_error", "rpc": f"{rpc:#010x}", "error": repr(e),
                      "payload": request.hex()})
            return None
        if inner is None:
            return None
        return [] if inner == NO_REPLY else data_chunks(msg.transfer, msg.channel, REPLY_FLAGS, inner)

    def _direct(self, header, body, addr) -> list[Packet]:
        peer = f"{addr[0]}:{addr[1]}"
        sess = self.sessions.get(addr)
        if sess is None or self.proven.get(addr) is not sess:
            return []
        self.log({"dir": "in", "src": peer, "kind": "direct", "payload": body.hex()})
        msg = parse_message(body)
        if not (msg and msg.crc_ok):
            return []
        if msg.type == T_DATA and msg.flags is not None and msg.flags & 1:
            if addr not in self.direct_partial and len(self.direct_partial) >= MAX_UNKNOWN:
                for pending in self.direct_partial.values():
                    release_all(pending)
                self.direct_partial.clear()
            request, received = assemble(self.direct_partial.setdefault(addr, {}), msg)
            if request is None or len(request) < 12:
                return []
            rpc = struct.unpack_from(">I", request, 4)[0]
            if rpc not in PLAINTEXT_REQUESTS or not self._may_request(addr):
                return []


            guest = self._new_ctx(DEFAULT_NAME, None)
            chunks = self._reply_chunks(msg, rpc, request, guest)
            if chunks is None:
                self.log({"dir": "in", "kind": "unhandled_rpc", "rpc": name_of(rpc), "target": "direct",
                          "payload": request.hex()})
                chunks = []
            else:
                self.say(addr, f"direct {name_of(rpc)} transfer {msg.transfer} -> plaintext reply {len(chunks)} chunk(s)")
            for c in chunks:
                self.log({"dir": "out", "dst": peer, "kind": "direct_reply", "plain": c.hex()})
            for a in self._ack(msg, received, guest):
                self.log({"dir": "out", "dst": peer, "kind": "direct_ack", "plain": a.hex()})
                chunks.append(a)
            return [(header + c, addr) for c in chunks]
        return []

    def _bind_player(self, sess: Session, addr: tuple[str, int], request: bytes) -> bool:
        name = player_name(request[12:])
        if not name:
            return True

        owner = (ticket := player_ticket(request[12:])) and ubi.ticket_owner(ticket, refresh=True)
        if not owner or account_key(owner[0]) != account_key(name):
            if not sess.refused:
                sess.refused = True
                self.say(addr, "GET_PLAYER with an unknown ticket: refused", always=True)
            return False
        name, platform = owner
        peers.PEERS.forget(player_id_for(name))
        sess.ctx = self.ctx_for_name(name) if self.save_path else self._new_ctx(name, None)
        sess.ctx.pc = platform == "uplay"
        sess.ctx.verified, sess.ctx.client_ip = True, addr[0]
        sess.name, sess.platform = name, platform
        self._unguest(addr)
        account = account_key(name)
        for other in [a for a, s in self.sessions.items() if a != addr and s.name and account_key(s.name) == account]:
            self._drop(other)
        self._remember(sess, addr)
        device = "PC" if platform == "uplay" else "PS4"
        save = sess.ctx.player.path
        print(f"[{_ts()}] {name} connected from {addr[0]} ({device})"
              + (f", save: {os.path.relpath(save, paths.HOME)}" if save else ""), flush=True)
        return True

    def _tunnel(self, sess, header, body, addr) -> list[Packet]:
        return self._tunnel_plain(sess, header, decrypt(sess.key, sess.iv, body), addr)

    def _tunnel_plain(self, sess, header, plain: bytes, addr) -> list[Packet]:
        peer = f"{addr[0]}:{addr[1]}"
        dst = (socket.inet_ntoa(header[:4]), struct.unpack(">H", header[4:6])[0])
        target = SERVICE_BY_ADDR.get(dst, f"{dst[0]}:{dst[1]}")
        msg = parse_message(plain)
        self.log({"dir": "in", "src": peer, "kind": "tunnel", "dst": f"{dst[0]}:{dst[1]}", "target": target,
                  "plain": plain.hex()})
        if not (msg and msg.crc_ok):
            return []
        if sess.ctx is not None and sess.name:
            ubi.touch(sess.ctx.player_name, addr[0])
            ubi.active(sess.name)
        if self.proven.get(addr) is not sess:
            self.proven[addr] = sess
            self.unproven.pop(addr, None)
        if msg.type == T_DATA and msg.flags is not None and msg.flags & 1:
            request, received = assemble(sess.partial, msg)
            if request is None or len(request) < 12:
                return []
            rpc = struct.unpack_from(">I", request, 4)[0]
            if not (self._bytes_ok(addr) and self._may_request(addr)):
                return []
            if not sess.name and rpc != GET_PLAYER and rpc not in self.guest_requests:

                self._guest_dropped(addr, f"{name_of(rpc)} transfer {msg.transfer} before a verified GET_PLAYER: "
                                          f"dropped")
                self.log({"dir": "in", "kind": "guest_dropped", "rpc": name_of(rpc), "target": target})
                return []
            first = (rpc, msg.transfer) not in sess.answered
            replied = sess.answered.get((rpc, msg.transfer))
            _keep_newest(sess.answered, (rpc, msg.transfer), replied)
            if not first and (rpc, msg.transfer) in sess.replies:

                chunks = list(sess.replies[(rpc, msg.transfer)])
            elif replied:


                self._evicted_dropped(addr, f"{name_of(rpc)} transfer {msg.transfer} resent, its reply is no longer "
                                            f"cached: dropped")
                return []
            else:
                if rpc == GET_PLAYER and not self._bind_player(sess, addr, request):
                    return []
                chunks = self._reply_chunks(msg, rpc, request, self.ctx_for(addr, sess))
                if chunks is not None:
                    sess.answered[(rpc, msg.transfer)] = True
                    self._cache_reply(sess, addr, (rpc, msg.transfer), list(chunks))
            if chunks is None:
                if first:
                    self.say(addr, f"UNHANDLED RPC {name_of(rpc)} to {target} transfer {msg.transfer} "
                                   f"payload {shown_hex(request)}")
                self.log({"dir": "in", "kind": "unhandled_rpc", "rpc": name_of(rpc), "target": target,
                          "payload": request.hex()})
                chunks = []
            elif first:
                self.say(addr, f"{name_of(rpc)} from {target} transfer {msg.transfer} -> reply "
                               f"{sum(map(len, chunks))}B in {len(chunks)} chunk(s)")
            self._charge_bytes(addr, sum(map(len, chunks)))
            for c in chunks:
                self.log({"dir": "out", "dst": peer, "kind": "rpc_reply", "rpc": name_of(rpc), "plain": c.hex()})
            for a in self._ack(msg, received, self.ctx_for(addr)):
                self.log({"dir": "out", "dst": peer, "kind": "ack", "rpc": name_of(rpc), "plain": a.hex()})
                chunks.append(a)
            return [(header + encrypt(sess.key, sess.iv, c), addr) for c in chunks]
        if msg.type == T_ACK and msg.received is not None:
            confirm = ack_confirm(msg)
            self.log({"dir": "out", "dst": peer, "kind": "ack_confirm", "plain": confirm.hex()})
            return [(header + encrypt(sess.key, sess.iv, confirm), addr)]
        if msg.type == T_RESOLVE and msg.length >= 25:


            ip = self.public_ip(addr)
            reply = resolve_reply(plain[12:msg.length], ip, self.port)
            self.say(addr, f"RESOLVE transfer {struct.unpack_from('>I', plain, 12)[0]} channel "
                           f"{name_of(struct.unpack_from('>I', plain, 16)[0])} -> {ip}:{self.port}")
            self.log({"dir": "out", "dst": peer, "kind": "resolve_reply", "plain": reply.hex()})
            return [(header + encrypt(sess.key, sess.iv, reply), addr)]
        if msg.type == T_ACK_CONFIRM:
            return []
        if msg.type == T_DATA:
            self.say(addr, f"DATA (no request) to {target} transfer {msg.transfer} flags {msg.flags}")
            self.log({"dir": "in", "src": peer, "kind": "data_no_request", "target": target, "plain": plain.hex()})
        elif msg.type != T_ACK:
            self.say(addr, f"UNKNOWN-FRAME 0x{msg.type:08X} to {target} ({msg.length}B)")
            self.log({"dir": "in", "src": peer, "kind": "unknown_frame", "target": target, "plain": plain.hex()})
        return []

    def serve_forever(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.ip, self.port))
        self.serve_socket(sock)

    def serve_socket(self, sock: socket.socket) -> None:
        no_connreset(sock)
        self.sender = self.sender or sock.sendto
        while True:
            try:
                data, addr = sock.recvfrom(65535)
            except OSError:
                if sock.fileno() == -1:
                    return
                continue
            try:
                failed = False
                for pkt, dst in self.handle_datagram(data, addr):
                    try:
                        sock.sendto(pkt, dst)
                    except OSError as e:
                        if not failed:
                            failed = True
                            self.say(dst, f"cannot send a reply: {e}")
            except Exception:
                try:
                    self.say(addr, f"handler error on {data[:32].hex()}\n{traceback.format_exc()}")
                    self.log({"dir": "in", "src": f"{addr[0]}:{addr[1]}", "kind": "error", "hex": data.hex(),
                              "error": traceback.format_exc()})
                except Exception:
                    pass
            finally:
                self.muted = False
                self.current = None
