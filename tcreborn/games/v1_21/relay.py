# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import datetime
import json
import os
import re
import shutil
import socket
import struct
import time
import traceback

from tcreborn.games.v1_21.crypto import IV0, K0, decrypt, encrypt
from tcreborn.discovery import local_ip_for
from tcreborn import config, paths, ubi
from tcreborn.saves import DEFAULT_NAME, save_name
from tcreborn.games.v1_21 import peers
from tcreborn.games.v1_21.handlers import NO_REPLY, Ctx, handle, name_of, player_id_for
from tcreborn.games.v1_21.player import ALL_ABILITIES, Player, dlc_fields, unlock_fields
from tcreborn.games.v1_21.handlers.replies import SERVICE_BY_ADDR
from tcreborn.session import STORE_NAME, Session, SessionStore
from tcreborn.wire import (DIRECT_HEADER, REPLY_FLAGS, T_ACK, T_ACK_CONFIRM, T_DATA, T_RESOLVE, ack, ack_confirm,
                        assemble, data_chunks, parse_message, resolve_reply)

Packet = tuple[bytes, tuple[str, int]]
NO_ACK_CHANNELS = {0x49466F25, 0x5ABA06B7}
GET_PLAYER = 0x589F57FB

_TICKET = re.compile(rb"b2ZmbGluZS10aWNrZXQ6[A-Za-z0-9+/=]*")
_KEYS = ("session_key", "session_iv")

GUEST_IDLE_S = 120
NAMED_IDLE_S = 15 * 60
MAX_GUESTS = 2000
MAX_UNKNOWN = 1000
_clock = time.monotonic


def hide_tickets(data: bytes) -> bytes:
    return _TICKET.sub(lambda m: b"*" * len(m.group()), data)


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
        self.guests: dict[tuple[str, int], None] = {}
        self.stored: set[tuple[str, int]] = set()
        self.swept = 0.0
        self.direct_partial: dict[tuple[str, int], dict] = {}
        self.save_path, self.abilities, self.unlocks, self.dlc = save_path, abilities, unlocks, dlc

        self.players: dict[str, Ctx] = {}
        self.ctx = self._new_ctx(DEFAULT_NAME, save_path)
        self.sender = sender
        self._restore()

    def _restore(self) -> None:
        restored = self.store.load() if self.store else {}
        if ubi.ACCOUNTS is not None:
            restored = {addr: sess for addr, sess in restored.items() if sess.name}
            self.stored = set(restored)
            for sess in restored.values():
                sess.seen = _clock()
        for addr, sess in restored.items():
            self.sessions[addr] = sess
            if ubi.ACCOUNTS is not None:
                continue
            if sess.name:
                ubi.SESSIONS.setdefault(addr[0], sess.name)
            if sess.platform:
                ubi.PLATFORMS.setdefault(addr[0], sess.platform)
        if restored:
            print(f"[{_ts()}] relay port {self.port}: {len(restored)} game session(s) from before the restart kept",
                  flush=True)

    def _remember(self, sess: Session | None, addr: tuple[str, int]) -> None:
        if not (self.store and sess is not None):
            return
        sess.stamp = time.time()
        if ubi.ACCOUNTS is None:
            self.store.save(self.sessions)
        elif sess.name or addr in self.stored:

            self._store_named()

    def _new_session(self, addr: tuple[str, int]) -> Session:
        sess = Session()
        if ubi.ACCOUNTS is None:
            self.sessions[addr] = sess
            return sess
        sess.seen = _clock()
        self.sessions.pop(addr, None)
        self.guests.pop(addr, None)
        self.sessions[addr] = sess
        self.guests[addr] = None
        while len(self.guests) > MAX_GUESTS:
            self._drop(next(iter(self.guests)))
        return sess

    def _expire(self, now: float) -> None:
        if now - self.swept < 1:
            return
        self.swept = now
        idle = [a for a, s in self.sessions.items() if now - s.seen > (NAMED_IDLE_S if s.name else GUEST_IDLE_S)]
        named = any(self.sessions[a].name for a in idle)
        for addr in idle + [a for a in self.guests if a not in self.sessions]:
            self._drop(addr)
        if named:
            self._store_named()

    def _drop(self, addr: tuple[str, int]) -> None:
        self.guests.pop(addr, None)
        self.sessions.pop(addr, None)
        self.direct_partial.pop(addr, None)

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
        if name not in self.players:
            folder = os.path.join(os.path.dirname(os.path.abspath(self.save_path)), "players")
            path = os.path.join(folder, save_name(name) + ".json")
            if ubi.ACCOUNTS is None and not os.path.exists(path) and os.path.exists(self.save_path) and not (
                    os.path.isdir(folder) and os.listdir(folder)):
                os.makedirs(folder, exist_ok=True)
                shutil.copyfile(self.save_path, path)
            self.players[name] = self._new_ctx(name, path)
        return self.players[name]

    def ctx_for(self, addr: tuple[str, int], sess: Session | None = None) -> Ctx:
        public = ubi.ACCOUNTS is not None
        if sess is None and public:
            sess = self.sessions.get(addr)
        if sess is not None and sess.ctx is not None:
            return sess.ctx

        restored = sess is not None and sess.name
        if public and not restored:


            ctx = self._new_ctx(DEFAULT_NAME, None)
            ctx.pc = ubi.PLATFORMS.get(addr[0]) == "uplay"
            if sess is not None:
                sess.ctx = ctx
            return ctx
        ctx = self.ctx_for_name(sess.name if restored else ubi.SESSIONS.get(addr[0]))
        if public:
            ctx.verified = True
        ctx.pc = (sess.platform if restored and sess.platform else ubi.PLATFORMS.get(addr[0])) == "uplay"
        if sess is not None:
            sess.ctx = ctx
        return ctx

    def _direct_guest(self, addr: tuple[str, int]) -> Ctx:
        guest = self._new_ctx(DEFAULT_NAME, None)
        sess = self.sessions.get(addr)
        guest.pc = sess.ctx.pc if sess is not None and sess.ctx is not None else \
            ubi.PLATFORMS.get(addr[0]) == "uplay"
        return guest

    def public_ip(self, peer: tuple[str, int]) -> str:
        if ubi.ACCOUNTS is not None and (advertised := config.advertised_ip()):
            return advertised
        return local_ip_for(peer[0]) if self.ip == "0.0.0.0" else self.ip

    def log(self, entry: dict) -> None:
        if not self.log_path:
            return
        if ubi.ACCOUNTS is not None:


            entry = {k: v for k, v in entry.items() if k not in _KEYS and not (k == "hex" and entry.get("step") == 2)}
            for k in ("hex", "plain", "payload"):
                if isinstance(entry.get(k), str):
                    entry[k] = hide_tickets(bytes.fromhex(entry[k])).hex()
        entry["time"] = datetime.datetime.now().isoformat()
        with open(self.log_path, "a") as f:
            f.write(json.dumps(entry) + "\n")

    def say(self, addr: tuple[str, int], text: str) -> None:
        print(f"[{_ts()}] {addr[0]}:{addr[1]} {text}", flush=True)

    def handle_datagram(self, data: bytes, addr: tuple[str, int]) -> list[Packet]:
        if addr == (self.ip, self.port):
            return []
        header, body = data[:6], data[6:]
        peer = f"{addr[0]}:{addr[1]}"
        if ubi.ACCOUNTS is not None:
            self._expire(_clock())
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
                if ubi.ACCOUNTS is not None and len(self.unknown) >= MAX_UNKNOWN:
                    self.unknown.clear()
                self.unknown.add(addr)
                self.say(addr, "packets of a game session this server does not know (no key): dropped until the "
                               "game connects again")
            return []
        sess.seen = _clock()
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

        sess = self._new_session(addr) if ubi.ACCOUNTS is not None else self.sessions.setdefault(addr, Session())
        sess.key, sess.iv, sess.connected = os.urandom(32), os.urandom(16), True
        reply = b"\x00\x00\x00\x00\x00\x01" + encrypt(*self.proxy_key, sess.key + sess.iv)
        self.say(addr, f"key request -> handshake 2 ({len(reply)}B)")
        self.log({"dir": "out", "dst": f"{addr[0]}:{addr[1]}", "step": 2, "hex": reply.hex(),
                  "session_key": sess.key.hex(), "session_iv": sess.iv.hex()})
        return [(reply, addr)]

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
        self.log({"dir": "in", "src": peer, "kind": "direct", "payload": body.hex()})
        msg = parse_message(body)
        if not (msg and msg.crc_ok):
            return []
        if msg.type == T_ACK and msg.received is not None:
            confirm = ack_confirm(msg)
            self.log({"dir": "out", "dst": peer, "kind": "direct_ack_confirm", "plain": confirm.hex()})
            return [(header + confirm, addr)]
        if msg.type == T_DATA and msg.flags is not None and msg.flags & 1:
            if ubi.ACCOUNTS is not None and addr not in self.direct_partial and \
                    len(self.direct_partial) >= MAX_UNKNOWN:
                self.direct_partial.clear()
            request, received = assemble(self.direct_partial.setdefault(addr, {}), msg)
            if request is None or len(request) < 12:
                return []
            rpc = struct.unpack_from(">I", request, 4)[0]


            guest = self._direct_guest(addr) if ubi.ACCOUNTS is not None else None
            chunks = self._reply_chunks(msg, rpc, request, guest or self.ctx_for(addr))
            if chunks is None:
                self.log({"dir": "in", "kind": "unhandled_rpc", "rpc": name_of(rpc), "target": "direct",
                          "payload": request.hex()})
                chunks = []
            else:
                self.say(addr, f"direct {name_of(rpc)} transfer {msg.transfer} -> plaintext reply {len(chunks)} chunk(s)")
            for c in chunks:
                self.log({"dir": "out", "dst": peer, "kind": "direct_reply", "plain": c.hex()})
            for a in self._ack(msg, received, guest or self.ctx_for(addr)):
                self.log({"dir": "out", "dst": peer, "kind": "direct_ack", "plain": a.hex()})
                chunks.append(a)
            return [(header + c, addr) for c in chunks]
        return []

    def _tunnel(self, sess, header, body, addr) -> list[Packet]:
        peer = f"{addr[0]}:{addr[1]}"
        dst = (socket.inet_ntoa(header[:4]), struct.unpack(">H", header[4:6])[0])
        target = SERVICE_BY_ADDR.get(dst, f"{dst[0]}:{dst[1]}")
        plain = decrypt(sess.key, sess.iv, body)
        msg = parse_message(plain)
        self.log({"dir": "in", "src": peer, "kind": "tunnel", "dst": f"{dst[0]}:{dst[1]}", "target": target,
                  "plain": plain.hex()})
        if sess.ctx is not None and (ubi.ACCOUNTS is None or sess.name):
            ubi.touch(sess.ctx.player_name, addr[0])
        if not (msg and msg.crc_ok):
            return []
        if msg.type == T_DATA and msg.flags is not None and msg.flags & 1:
            request, received = assemble(sess.partial, msg)
            if request is None or len(request) < 12:
                return []
            rpc = struct.unpack_from(">I", request, 4)[0]
            first = (rpc, msg.transfer) not in sess.answered
            sess.answered.add((rpc, msg.transfer))
            if not first and (rpc, msg.transfer) in sess.replies:

                chunks = list(sess.replies[(rpc, msg.transfer)])
            else:
                name = player_name(request[12:]) if rpc == GET_PLAYER else None
                platform = ubi.PLATFORMS.get(addr[0])
                if name and ubi.ACCOUNTS is not None:

                    owner = (ticket := player_ticket(request[12:])) and ubi.ticket_owner(ticket)
                    if not owner or save_name(owner[0]).lower() != save_name(name).lower():
                        if not sess.refused:
                            sess.refused = True
                            self.say(addr, "GET_PLAYER with an unknown ticket: refused")
                        return []
                    name, platform = owner
                if name:
                    peers.PEERS.forget(player_id_for(name))
                if name and (self.save_path or ubi.ACCOUNTS is not None):
                    sess.ctx = self.ctx_for_name(name) if self.save_path else self._new_ctx(name, None)
                    if ubi.ACCOUNTS is not None:
                        sess.ctx.pc = platform == "uplay"
                        sess.ctx.verified = True
                    sess.name, sess.platform = name, platform
                    self.guests.pop(addr, None)
                    if ubi.ACCOUNTS is not None:
                        account = save_name(name).lower()
                        for other in [a for a, s in self.sessions.items()
                                      if a != addr and s.name and save_name(s.name).lower() == account]:
                            self._drop(other)
                    self._remember(sess, addr)
                    device = "PC" if platform == "uplay" else "PS4"
                    save = sess.ctx.player.path
                    print(f"[{_ts()}] {name} connected from {addr[0]} ({device})"
                          + (f", save: {os.path.relpath(save, paths.HOME)}" if save else ""), flush=True)
                chunks = self._reply_chunks(msg, rpc, request, self.ctx_for(addr, sess))
                if chunks is not None:
                    sess.replies[(rpc, msg.transfer)] = list(chunks)
            if chunks is None:
                if first:
                    self.say(addr, f"UNHANDLED RPC {name_of(rpc)} to {target} transfer {msg.transfer} "
                                   f"payload {request.hex()}")
                self.log({"dir": "in", "kind": "unhandled_rpc", "rpc": name_of(rpc), "target": target,
                          "payload": request.hex()})
                chunks = []
            elif first:
                self.say(addr, f"{name_of(rpc)} from {target} transfer {msg.transfer} -> reply "
                               f"{sum(map(len, chunks))}B in {len(chunks)} chunk(s)")
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
        self.sender = self.sender or sock.sendto
        while True:
            data, addr = sock.recvfrom(65535)
            try:
                for pkt, dst in self.handle_datagram(data, addr):
                    sock.sendto(pkt, dst)
            except Exception:
                self.say(addr, f"handler error on {data[:32].hex()}\n{traceback.format_exc()}")
                self.log({"dir": "in", "src": f"{addr[0]}:{addr[1]}", "kind": "error", "hex": data.hex(),
                          "error": traceback.format_exc()})
