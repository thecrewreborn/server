# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import datetime
import socket
import threading

DISCOVERY_PORT = 4001
PROBE = b"TCPS4?"
ANSWER = b"TCPS4!"


def local_ip_for(peer: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((peer, 9))
        return s.getsockname()[0]


BOTS, AFK, DLC = 1, 2, 4


def answer(probe: bytes, peer: str, relay_port: int, http_port: int, advertise: str | None = None,
           bots: bool = False, afk: bool = False, https_port: int = 0, dlc: bool = False) -> bytes | None:
    if not probe.startswith(PROBE):
        return None
    ip = advertise or local_ip_for(peer)
    return (ANSWER + socket.inet_aton(ip) + relay_port.to_bytes(2, "big") + http_port.to_bytes(2, "big")
            + bytes([(BOTS if bots else 0) | (AFK if afk else 0) | (DLC if dlc else 0)])
            + (https_port.to_bytes(2, "big") if https_port else b""))


def serve(relay_port: int, http_port: int, advertise: str | None = None, port: int = DISCOVERY_PORT,
          bots: bool = False, afk: bool = False, https_port: int = 0, dlc: bool = False):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("0.0.0.0", port))

    def run() -> None:
        while True:
            data, addr = sock.recvfrom(512)
            reply = answer(data, addr[0], relay_port, http_port, advertise, bots, afk, https_port, dlc)
            if reply:
                sock.sendto(reply, addr)
                client = data[len(PROBE):].decode("ascii", "replace").strip() or "no version"
                print(f"[{datetime.datetime.now():%H:%M:%S}] discovery {addr[0]} ({client}) -> "
                      f"{socket.inet_ntoa(reply[6:10])}", flush=True)
    threading.Thread(target=run, daemon=True).start()
    return sock
