# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import os
import re
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tcreborn import discovery, server_id
from tcreborn.version import VERSION

KEY_LINE = re.compile(rb"[0-9a-f]{32}(\r?\n)?")


class NoServerId(Exception):
    pass


def read_key(arg: str) -> str:
    if os.path.isfile(arg):
        with open(arg, "rb") as f:
            raw = f.read(64)
        source = "the key file"
    else:
        raw = arg.strip().encode()
        source = "the key"
    if not KEY_LINE.fullmatch(raw):
        raise ValueError(f"{source} is not 32 lowercase hex characters (the PS4 makes a new key for such a file)")
    return raw[:32].decode()


def host_of(text: str) -> str:
    line = (text.strip().splitlines() or [""])[0].strip()
    line = re.sub(r"^[a-zA-Z][a-zA-Z0-9+.-]*://", "", line)
    host = line.split("/", 1)[0].split(":", 1)[0]
    if not host:
        raise ValueError(f"no server address in {text.strip()!r}")
    return host


def port_of(text: str) -> int:
    line = (text.strip().splitlines() or [""])[0].strip()
    line = re.sub(r"^[a-zA-Z][a-zA-Z0-9+.-]*://", "", line).split("/", 1)[0]
    if ":" not in line:
        return 443
    port = line.split(":", 1)[1]
    if not re.fullmatch(r"[0-9]{1,5}", port) or not 1 <= int(port) <= 65535:
        raise ValueError(f"no HTTPS port in {text.strip()!r}")
    return int(port)


def plain_http(text: str) -> bool:
    return bool(re.match(r"\s*http://", text, re.IGNORECASE))


def ipv4_of(host: str) -> str:
    return socket.getaddrinfo(host, discovery.DISCOVERY_PORT, socket.AF_INET, socket.SOCK_DGRAM)[0][4][0]


def password(key: str, server: bytes, address: str, port: int = 443) -> str:
    return server_id.console_code(bytes.fromhex(key), server, address, port)


def announced_https_port(reply: bytes) -> int:
    return int.from_bytes(reply[15:17], "big") if len(reply) >= 17 else 0


def announced(reply: bytes) -> str:
    return socket.inet_ntoa(reply[6:10])


def server_id_of(reply: bytes) -> bytes:
    if not reply.startswith(discovery.ANSWER) or len(reply) < 17 + server_id.SIZE:
        raise NoServerId
    return reply[17:17 + server_id.SIZE]


def fetch_answer(address: str, port: int | None = None, timeout: float = 1.0, attempts: int = 3) -> bytes:
    port = port or discovery.DISCOVERY_PORT
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(timeout)
        for _ in range(attempts):
            s.sendto(discovery.PROBE + f"tool {VERSION}".encode(), (address, port))
            try:
                while True:
                    reply, src = s.recvfrom(512)
                    if src[0] == address and reply.startswith(discovery.ANSWER):
                        return reply
            except socket.timeout:
                continue
    raise OSError(f"no answer from {address} on UDP port {port} (the server's discovery port must be reachable, "
                  f"or give --server-id)")


def fetch_server_id(address: str, port: int | None = None, timeout: float = 1.0, attempts: int = 3) -> bytes:
    return server_id_of(fetch_answer(address, port, timeout, attempts))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Print the PC password for a name claimed with a PS4 on one server. The server is asked for its "
                    "id on UDP port 4001; the HTTPS port in SERVER (default 443) is part of the code.")
    ap.add_argument("key", help="console_key.txt from the PS4 (/data/TheCrew), or its 32 hex characters")
    ap.add_argument("server", nargs="?",
                    help="the server exactly as in server.txt: host, host:port or https://host[:port]")
    ap.add_argument("--server-id", help="the server's id (32 hex characters, server_id of /version) instead of asking "
                                        "it; the server's address is needed all the same")
    args = ap.parse_args(argv)
    try:
        key = read_key(args.key)
        if not args.server:
            raise ValueError("give the server's address (the code is bound to it)")
        if plain_http(args.server):
            raise ValueError("a plain-HTTP server (http://) gets no code from the PS4 (it sends OFFLINE); use HTTPS")
        address, port = ipv4_of(host_of(args.server)), port_of(args.server)
        if address.startswith("127."):
            print(f"note: {address} is a loopback address. The PS4 binds its code to the address it talks to: the one "
                  f"in its server.txt, else the address the server announces on the LAN. Give that address, not "
                  f"localhost.", file=sys.stderr)
        if args.server_id:
            sid = bytes.fromhex(args.server_id.strip())
            if len(sid) != server_id.SIZE:
                raise ValueError("--server-id must be 32 hex characters")
        else:
            reply = fetch_answer(address)
            sid = server_id_of(reply)
            if len(reply) >= 10 and announced(reply) != address:
                print(f"note: the server announces {announced(reply)}. A PS4 that found it on the LAN (no server.txt) "
                      f"talks to that address; for such a console run this again with {announced(reply)}.",
                      file=sys.stderr)
            if announced_https_port(reply) not in (0, port):
                print(f"note: the server announces HTTPS port {announced_https_port(reply)}. A PS4 that found it on "
                      f"the LAN (no server.txt) talks to that port; for such a console run this again with "
                      f"{announced(reply)}:{announced_https_port(reply)}.", file=sys.stderr)
    except NoServerId:
        print("the server sent no server id: it is older than 0.0.4, and PS4 patches send it no key at all",
              file=sys.stderr)
        return 2
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    print(password(key, sid, address, port))
    return 0


if __name__ == "__main__":
    sys.exit(main())
