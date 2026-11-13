# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import collections
import functools
import os
import struct
import time
import types

from tcreborn.games.v1_21 import relay
from tcreborn.keys import K0
from tcreborn.games.v1_00 import PORT_OFFSET, crypto, handlers
from tcreborn.wire import assemble, data_chunk

DUPLICATE_WINDOW = 0.5


GET_OR_CREATE_PLAYER = 0x5EAA9404
CHUNK = 1000
T_NACK = 0xEF318FDD
RESEND_WINDOW = 0.2
TABLE_MAX = 65536


GUEST_REQUESTS = frozenset({
    0x036D7773,
    0x27C711C5,
    0x2E0E24CB,
    0x368D58C9,
    0x40BD5E2B,
    0x720CC96E,
    0xB1F54E5C,
})


def _note(table: collections.OrderedDict, key, now: float, window: float) -> None:
    table[key] = now
    table.move_to_end(key)
    while table:
        first = next(iter(table.values()))
        if now - first < window and len(table) <= TABLE_MAX:
            break
        table.popitem(last=False)


def _cached_reply(replies: dict, transfer: int, channel: int) -> list[bytes] | None:
    def on(chunks) -> bool:
        return bool(chunks) and len(chunks[0]) >= 20 and struct.unpack_from(">I", chunks[0], 16)[0] == channel
    chunks = replies.get((channel, transfer))
    if on(chunks):
        return chunks
    return next((c for (_rpc, t), c in reversed(replies.items()) if t == transfer and on(c)), None)


def _encrypt(key: bytes, _iv: bytes, data: bytes) -> bytes:
    return crypto.encrypt(key, data)


def _decrypt(key: bytes, _iv: bytes, data: bytes) -> bytes:
    return crypto.decrypt(key, data)


def data_chunks(transfer: int, channel: int, flags: int, inner: bytes) -> list[bytes]:
    total = len(inner)
    return [data_chunk(transfer, channel, flags, inner[i * CHUNK:(i + 1) * CHUNK], i, total)
            for i in range(total // CHUNK + 1)]


def _v100(fn):
    names = dict(fn.__globals__, encrypt=_encrypt, decrypt=_decrypt, data_chunks=data_chunks,
                 handle=handlers.handle, name_of=handlers.name_of, GET_PLAYER=GET_OR_CREATE_PLAYER,
                 assemble=functools.partial(assemble, chunk=CHUNK))
    return types.FunctionType(fn.__code__, names, fn.__name__, fn.__defaults__, fn.__closure__)


class V100Server(relay.Server):

    guest_requests = GUEST_REQUESTS

    _tunnel_plain = _v100(relay.Server._tunnel_plain)

    def _tunnel(self, sess, header, body, addr):
        plain = crypto.decrypt(sess.key, body)
        msg = relay.parse_message(plain)
        if msg and msg.crc_ok and msg.type == T_NACK:
            return self._resend(sess, header, plain[12:msg.length], addr)
        if msg and msg.crc_ok and msg.type == relay.T_DATA and msg.flags is not None and msg.flags & 1:
            key, now = (addr, msg.transfer, msg.channel, msg.chunk), time.monotonic()
            if now - self.seen.get(key, -1e9) < DUPLICATE_WINDOW:
                return []
            _note(self.seen, key, now, DUPLICATE_WINDOW)
        out = self._tunnel_plain(sess, header, plain, addr)
        if msg and msg.crc_ok and msg.type == relay.T_DATA and msg.flags is not None and msg.flags & 3 == 1:


            kept = [(pkt, dst) for pkt, dst in out
                    if (m := relay.parse_message(crypto.decrypt(sess.key, pkt[6:]))) is None or m.type != relay.T_DATA]
            if len(kept) < len(out):
                self.say(addr, f"transfer {msg.transfer}: request sent with flags 1, its reply is not sent")
            out = kept
        return out

    def _resend(self, sess, header, body: bytes, addr):
        if len(body) < 14:
            return []
        transfer, channel = struct.unpack_from(">II", body, 0)
        chunks = _cached_reply(sess.replies, transfer, channel)
        if not chunks:
            return []
        (n,) = struct.unpack_from(">I", body, 10)
        array = body[14:14 + n]
        count = struct.unpack_from(">I", array, 12)[0] if len(array) >= 16 else 0
        words = struct.unpack_from(f">{count}I", array, 16) if len(array) >= 16 + 4 * count else ()
        missing, k = set(), 0
        while k < len(words):
            w = words[k]
            if w & 0x80000000 and k + 1 < len(words):
                missing.update(range(w & 0x7FFFFFFF, min(words[k + 1] + 1, len(chunks))))
                k += 2
            else:
                if w & 0x7FFFFFFF < len(chunks):
                    missing.add(w & 0x7FFFFFFF)
                k += 1
        now = time.monotonic()
        due = [i for i in sorted(missing) if now - self.resent.get((addr, transfer, channel, i), -1e9) >= RESEND_WINDOW]
        if not due or not self._bytes_ok(addr) or not self._may_request(addr):
            return []
        self._charge_bytes(addr, sum(len(chunks[i]) for i in due))
        out = []
        for i in due:
            _note(self.resent, (addr, transfer, channel, i), now, RESEND_WINDOW)
            out.append((header + crypto.encrypt(sess.key, chunks[i]), addr))
        if out:
            self.say(addr, f"NACK transfer {transfer} -> resent {len(out)} of {len(chunks)} chunk(s)")
            self.log({"dir": "out", "dst": f"{addr[0]}:{addr[1]}", "kind": "resend", "transfer": transfer,
                      "chunks": sorted(missing)})
        return out
    _reply_chunks = _v100(relay.Server._reply_chunks)
    _direct = _v100(relay.Server._direct)

    def __init__(self, ip: str, port: int, log_path: str | None, save_path: str | None, key: bytes = K0, **kw):
        super().__init__(ip, port, log_path, save_path, dlc="none", **kw)
        self.key = key


        self.seen: collections.OrderedDict = collections.OrderedDict()
        self.resent: collections.OrderedDict = collections.OrderedDict()

    def _probe(self, addr):
        self._new_session(addr)
        plain = relay._addr_bytes(self.public_ip(addr), self.port) + relay._addr_bytes(*addr) + struct.pack(">II", 1, 1)
        reply = b"\x00" * 6 + crypto.encrypt(self.key, plain)
        self.say(addr, f"01.00 probe -> handshake 1 ({len(reply)}B)")
        self.log({"dir": "out", "dst": f"{addr[0]}:{addr[1]}", "step": 1, "hex": reply.hex()})
        return [(reply, addr)]

    def _key_exchange(self, addr):

        sess = self._new_session(addr)
        sess.key, sess.iv, sess.connected = os.urandom(32), b"", True
        self.muted = False
        reply = b"\x00\x00\x00\x00\x00\x01" + crypto.encrypt(self.key, sess.key)
        self.say(addr, f"01.00 key request -> handshake 2 ({len(reply)}B)")
        self.log({"dir": "out", "dst": f"{addr[0]}:{addr[1]}", "step": 2, "hex": reply.hex(),
                  "session_key": sess.key.hex()})
        return [(reply, addr)]

    def say(self, addr, text: str, always: bool = False) -> None:
        super().say(addr, f"[1.00] {text}", always)
