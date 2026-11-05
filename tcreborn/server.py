# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import datetime
import json
import os
import re
import shutil
import socket
import struct
import traceback

from tcreborn.crypto import IV0, K0, decrypt, encrypt
from tcreborn.discovery import local_ip_for
from tcreborn import peers, ubi
from tcreborn.handlers import NO_REPLY, Ctx, handle, name_of, player_id_for
from tcreborn.player import ALL_ABILITIES, Player, dlc_fields, unlock_fields
from tcreborn.handlers.replies import SERVICE_BY_ADDR
from tcreborn.session import Session
from tcreborn.wire import (DIRECT_HEADER, REPLY_FLAGS, T_ACK, T_ACK_CONFIRM, T_DATA, T_RESOLVE, ack, ack_confirm,
                        assemble, data_chunks, parse_message, resolve_reply)

Packet = tuple[bytes, tuple[str, int]]
NO_ACK_CHANNELS = {0x49466F25, 0x5ABA06B7}
GET_PLAYER = 0x589F57FB
DEFAULT_NAME = "Player"


def player_name(get_player_body: bytes) -> str | None:
    if len(get_player_body) < 0x30:
        return None
    n = struct.unpack_from(">I", get_player_body, 0x2C)[0]
    raw = get_player_body[0x30:0x30 + n]
    return raw.decode("utf-8", "replace") if 0 < n <= 64 and len(raw) == n else None


def save_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_-]", "_", name)[:32] or DEFAULT_NAME


def _ts() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def _addr_bytes(ip: str, port: int) -> bytes:
    return socket.inet_aton(ip) + struct.pack(">H", port)


class Server:
    def __init__(self, ip: str, port: int, log_path: str | None, save_path: str | None = None,
                 sender=None, abilities: str = "none",
                 unlocks: str = "none", dlc: str = "all",
                 proxy_key: tuple[bytes, bytes] = (K0, IV0)):
        self.ip, self.port, self.log_path = ip, port, log_path
        self.proxy_key = proxy_key
        self.sessions: dict[tuple[str, int], Session] = {}
        self.direct_partial: dict[tuple[str, int], dict] = {}
        self.save_path, self.abilities, self.unlocks, self.dlc = save_path, abilities, unlocks, dlc

        self.players: dict[str, Ctx] = {}
        self.ctx = self._new_ctx(DEFAULT_NAME, save_path)
        self.sender = sender

    def _new_ctx(self, name: str, path: str | None) -> Ctx:
        ctx = Ctx(player_name=name, player_id=player_id_for(name), player=Player.load(path))
        if self.abilities == "all" and not ctx.player.abilities:
            ctx.player.abilities = list(ALL_ABILITIES)
            ctx.player.save()
        ctx.player.unlocks = unlock_fields(self.unlocks)
        ctx.player.dlc = dlc_fields(self.dlc)
        return ctx

    def ctx_for_name(self, name: str | None) -> Ctx:
        if not name or name == DEFAULT_NAME or not self.save_path:
            return self.ctx
        if name not in self.players:
            folder = os.path.join(os.path.dirname(os.path.abspath(self.save_path)), "players")
            path = os.path.join(folder, save_name(name) + ".json")
            if not os.path.exists(path) and os.path.exists(self.save_path) and not (
                    os.path.isdir(folder) and os.listdir(folder)):
                os.makedirs(folder, exist_ok=True)
                shutil.copyfile(self.save_path, path)
            self.players[name] = self._new_ctx(name, path)
        return self.players[name]

    def ctx_for(self, addr: tuple[str, int], sess: Session | None = None) -> Ctx:
        if sess is not None and sess.ctx is not None:
            return sess.ctx
        ctx = self.ctx_for_name(ubi.SESSIONS.get(addr[0]))
        ctx.pc = ubi.PLATFORMS.get(addr[0]) == "uplay"
        if sess is not None:
            sess.ctx = ctx
        return ctx

    def public_ip(self, peer: tuple[str, int]) -> str:
        return local_ip_for(peer[0]) if self.ip == "0.0.0.0" else self.ip

    def log(self, entry: dict) -> None:
        if not self.log_path:
            return
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
        self.log({"dir": "in", "src": peer, "hex": data.hex()})
        if len(data) == 6 and header[4:6] == b"\x00\x00":
            return self._probe(addr)
        if len(data) == 6 and header[4:6] == b"\x00\x01":
            return self._key_exchange(addr)
        if header == DIRECT_HEADER:
            return self._direct(header, body, addr)
        sess = self.sessions.get(addr)
        if not (sess and sess.connected and body):
            self.log({"dir": "in", "src": peer, "kind": "unknown"})
            return []
        return self._tunnel(sess, header, body, addr)

    def _probe(self, addr) -> list[Packet]:
        self.sessions[addr] = Session()
        plain = _addr_bytes(self.public_ip(addr), self.port) + _addr_bytes(*addr) + struct.pack(">II", 1, 1)
        reply = b"\x00" * 6 + encrypt(*self.proxy_key, plain)
        self.say(addr, f"probe -> handshake 1 ({len(reply)}B)")
        self.log({"dir": "out", "dst": f"{addr[0]}:{addr[1]}", "step": 1, "hex": reply.hex()})
        return [(reply, addr)]

    def _key_exchange(self, addr) -> list[Packet]:
        sess = self.sessions.setdefault(addr, Session())
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
            request, received = assemble(self.direct_partial.setdefault(addr, {}), msg)
            if request is None or len(request) < 12:
                return []
            rpc = struct.unpack_from(">I", request, 4)[0]
            chunks = self._reply_chunks(msg, rpc, request, self.ctx_for(addr))
            if chunks is None:
                self.log({"dir": "in", "kind": "unhandled_rpc", "rpc": name_of(rpc), "target": "direct",
                          "payload": request.hex()})
                chunks = []
            else:
                self.say(addr, f"direct {name_of(rpc)} transfer {msg.transfer} -> plaintext reply {len(chunks)} chunk(s)")
            for c in chunks:
                self.log({"dir": "out", "dst": peer, "kind": "direct_reply", "plain": c.hex()})
            for a in self._ack(msg, received, self.ctx_for(addr)):
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
                if rpc == GET_PLAYER and (name := player_name(request[12:])):
                    peers.PEERS.forget(player_id_for(name))
                if rpc == GET_PLAYER and (name := player_name(request[12:])) and self.save_path:
                    sess.ctx = self.ctx_for_name(name)
                    self.say(addr, f"player {name}")
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
        print(f"[{_ts()}] relay listening on udp {self.ip}:{self.port}", flush=True)
        while True:
            data, addr = sock.recvfrom(65535)
            try:
                for pkt, dst in self.handle_datagram(data, addr):
                    sock.sendto(pkt, dst)
            except Exception:
                self.say(addr, f"handler error on {data[:32].hex()}\n{traceback.format_exc()}")
                self.log({"dir": "in", "src": f"{addr[0]}:{addr[1]}", "kind": "error", "hex": data.hex(),
                          "error": traceback.format_exc()})
