# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import collections
import socket
import struct
import threading
import zlib
from dataclasses import dataclass

T_DATA = 0xCE57ECDE
T_ACK = 0xD8191434
T_ACK_CONFIRM = 0x32000A70
T_ARRAY = zlib.crc32(b"SerializerArray")
T_LIST = zlib.crc32(b"SerializerList")
REPLY_FLAGS = 0x0a
CHUNK = 1200
DIRECT_HEADER = b"\xff" * 6


def frame(type_id: int, body: bytes) -> bytes:
    return struct.pack(">III", 12 + len(body), type_id, zlib.crc32(body)) + body


def blob(data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + data


def empty_array() -> bytes:
    return blob(frame(T_ARRAY, struct.pack(">I", 0)))


def u64_array(words: list[int]) -> bytes:
    return blob(frame(T_ARRAY, struct.pack(">I", len(words)) + b"".join(struct.pack(">Q", w) for w in words)))


def serializer_list(elems: list[bytes]) -> bytes:
    return frame(T_LIST, b"".join(blob(e) for e in elems))


@dataclass
class Message:
    type: int
    length: int
    crc_ok: bool
    transfer: int | None = None
    channel: int | None = None
    flags: int | None = None
    chunk: int | None = None
    total: int | None = None
    payload: bytes | None = None
    kind: int | None = None
    stamp: bytes | None = None
    received: int | None = None


def parse_message(plain: bytes) -> Message | None:
    if len(plain) < 12:
        return None
    length, type_id, crc = struct.unpack_from(">III", plain)
    msg = plain[:length]
    m = Message(type_id, length, len(msg) == length and zlib.crc32(msg[12:]) == crc)
    if type_id == T_DATA and length >= 33:
        m.transfer, m.channel, m.flags, m.chunk, m.total = struct.unpack_from(">IIBII", msg, 12)
        m.payload = msg[33:]
    elif type_id == T_ACK and length >= 49:
        m.transfer, m.channel, m.flags, m.kind = struct.unpack_from(">IIBI", msg, 12)
        m.stamp = msg[25:33]
        m.received = struct.unpack_from(">I", msg, 45)[0]
    return m


def data_chunk(transfer: int, channel: int, flags: int, payload: bytes, index: int, total: int) -> bytes:
    return frame(T_DATA, struct.pack(">IIBII", transfer, channel, flags, index, total) + blob(payload))


def data_chunks(transfer: int, channel: int, flags: int, inner: bytes) -> list[bytes]:

    total = len(inner)
    return [data_chunk(transfer, channel, flags, inner[i * CHUNK:(i + 1) * CHUNK], i, total)
            for i in range(total // CHUNK + 1)]


def ack(transfer: int, channel: int, flags: int, stamp: int, received: int = 1) -> bytes:
    body = struct.pack(">IIBIQQII", transfer, channel, flags, 1, stamp, 0, 0, received)
    return frame(T_ACK, body)


def ack_confirm(ack: Message) -> bytes:
    body = struct.pack(">IIBI", ack.transfer, ack.channel, ack.flags, ack.kind) + ack.stamp
    return frame(T_ACK_CONFIRM, body + struct.pack(">I", ack.received))


T_RESOLVE = 0xC7872821
T_RESOLVE_REPLY = 0x8048BEAC


def resolve_reply(request_body: bytes, ip: str, port: int) -> bytes:
    return frame(T_RESOLVE_REPLY, request_body[:13] + socket.inet_aton(ip) + struct.pack(">H", port))


MAX_REQUEST = 65536
MAX_PENDING = 16


MAX_PENDING_BYTES = 64 * 2 ** 20
_COST_TRANSFER, _COST_CHUNK = 512, 128
_lock = threading.Lock()


_held: collections.OrderedDict = collections.OrderedDict()
_held_bytes = 0


def pending_bytes() -> int:
    return _held_bytes


def _charge(home: dict, key, entry: list, cost: int) -> None:
    global _held_bytes
    record = _held.get(id(entry))
    if record is None:
        record = _held[id(entry)] = [home, key, entry, 0]
    record[3] += cost
    _held_bytes += cost


def _release(entry: list) -> None:
    global _held_bytes
    record = _held.pop(id(entry), None)
    if record is None:
        return
    _held_bytes -= record[3]
    home, key = record[0], record[1]
    if home.get(key) is entry:
        del home[key]


def release_all(partial: dict) -> None:
    with _lock:
        for entry in list(partial.values()):
            _release(entry)
        partial.clear()


def assemble(partial: dict, msg, chunk: int = CHUNK) -> tuple[bytes | None, int]:
    if msg.total > MAX_REQUEST or msg.chunk > msg.total // chunk or len(msg.payload) > chunk:
        return None, 0
    key = (msg.transfer, msg.channel)
    with _lock:
        entry = partial.get(key)
        if entry is None:
            while len(partial) >= MAX_PENDING:
                oldest = next(iter(partial))
                _release(partial[oldest])
                partial.pop(oldest, None)
            entry = partial[key] = [{}, 0]
            _charge(partial, key, entry, _COST_TRANSFER)
        parts = entry[0]
        old = parts.get(msg.chunk)
        parts[msg.chunk] = msg.payload
        grown = len(msg.payload) - (len(old) if old is not None else 0)
        entry[1] += grown
        _charge(partial, key, entry, grown + (_COST_CHUNK if old is None else 0))
        if entry[1] < msg.total:
            while _held_bytes > MAX_PENDING_BYTES and _held:
                _release(next(iter(_held.values()))[2])
            return None, 0
        _release(entry)
    return b"".join(parts[i] for i in sorted(parts))[:msg.total], len(parts)
