# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import datetime
import socket
import threading
import time

from tcreborn import server_id as server_ids
from tcreborn.limits import RateLimit

DISCOVERY_PORT = 4001
PROBE = b"TCPS4?"
ANSWER = b"TCPS4!"


def local_ip_for(peer: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((peer, 9))
        return s.getsockname()[0]


BOTS, AFK, DLC = 1, 2, 4


def answer(probe: bytes, peer: str, relay_port: int, http_port: int, advertise: str | None = None,
           bots: bool = False, afk: bool = False, https_port: int = 0, dlc: bool = False,
           server_id: bytes = b"") -> bytes | None:
    if not probe.startswith(PROBE):
        return None
    ip = advertise or local_ip_for(peer)
    reply = (ANSWER + socket.inet_aton(ip) + relay_port.to_bytes(2, "big") + http_port.to_bytes(2, "big")
             + bytes([(BOTS if bots else 0) | (AFK if afk else 0) | (DLC if dlc else 0)]))
    if https_port or server_id:
        reply += https_port.to_bytes(2, "big")
    return reply + server_id


def no_connreset(sock) -> None:
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        try:
            sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        except (OSError, ValueError):
            pass


REPLIES_PER_S = 1
LINES_PER_MIN = 20
_clock = time.monotonic


class _Lines:

    def __init__(self):
        self.window, self.count, self.left_out = -1e9, 0, 0

    def may_print(self, now: float) -> bool:
        if now - self.window >= 60:
            if self.left_out:
                print(f"[{datetime.datetime.now():%H:%M:%S}] discovery: {self.left_out} line(s) suppressed",
                      flush=True)
            self.window, self.count, self.left_out = now, 0, 0
        if self.count >= LINES_PER_MIN:
            self.left_out += 1
            return False
        self.count += 1
        return True


def run(sock, relay_port: int, http_port: int, advertise: str | None = None, bots: bool = False,
        afk: bool = False, https_port: int = 0, dlc: bool = False, server_id: bytes | None = None) -> None:
    no_connreset(sock)
    if server_id is None:
        server_id = server_ids.get()
    replies, lines = RateLimit(REPLIES_PER_S, 1.0), _Lines()
    while True:
        try:
            data, addr = sock.recvfrom(512)
        except OSError:
            if sock.fileno() == -1:
                return
            continue
        try:
            reply = answer(data, addr[0], relay_port, http_port, advertise, bots, afk, https_port, dlc, server_id)
            if reply:
                now = _clock()
                if not replies.allow(addr[0], now):
                    continue
                sock.sendto(reply, addr)
                if not lines.may_print(now):
                    continue
                client = data[len(PROBE):].decode("ascii", "replace").strip() or "no version"
                client = "".join(c if c.isprintable() else "?" for c in client[:64])
                print(f"[{datetime.datetime.now():%H:%M:%S}] discovery {addr[0]} ({client}) -> "
                      f"{socket.inet_ntoa(reply[6:10])}", flush=True)
        except Exception:
            continue


def serve(relay_port: int, http_port: int, advertise: str | None = None, port: int = DISCOVERY_PORT,
          bots: bool = False, afk: bool = False, https_port: int = 0, dlc: bool = False,
          server_id: bytes | None = None):
    if server_id is None:
        server_id = server_ids.get()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("0.0.0.0", port))
    threading.Thread(target=run, args=(sock, relay_port, http_port, advertise, bots, afk, https_port, dlc,
                                       server_id), daemon=True).start()
    return sock
