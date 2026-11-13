# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import hashlib
import hmac
import os
import re
import socket
import threading

from tcreborn import files, paths

PATH = paths.home("save", "server_id")
SIZE = 16
_cache: tuple[str, bytes] | None = None
_lock = threading.Lock()


class Damaged(OSError):
    pass


_FREE_WITH = ("then every name a PS4 claimed on this server must be freed with tools/accounts.py and claimed again). "
              "The server does not start.")


def _bad_files(path: str) -> list[str]:
    folder, name = os.path.split(path)
    try:
        names = os.listdir(folder or ".")
    except FileNotFoundError:
        return []
    bad = name + ".bad"
    numbered = re.compile(re.escape(bad) + r"\.([1-9][0-9]*)")
    numbers = sorted(int(m.group(1)) for m in map(numbered.fullmatch, names) if m)
    found = ([bad] if bad in names else []) + [f"{bad}.{i}" for i in numbers]
    return [os.path.join(folder, n) for n in found]


def _keep_damaged(path: str) -> str:
    bad, i = path + ".bad", 0
    while os.path.lexists(bad):
        i += 1
        bad = f"{path}.bad.{i}"
    os.rename(path, bad)
    return bad


def _read(path: str) -> bytes | None:
    try:
        with open(path, "rb") as f:
            raw = f.read(4 * SIZE)
    except FileNotFoundError:
        return None
    try:
        value = bytes.fromhex(raw.decode("ascii").strip())
    except ValueError:
        return b""
    return value if len(value) == SIZE else b""


def get() -> bytes:
    global _cache
    with _lock:
        path = PATH
        if _cache and _cache[0] == path:
            return _cache[1]
        value = _read(path)
        if value == b"":
            bad = _keep_damaged(path)
            raise Damaged(f"{path} is damaged and was kept as {bad}. Restore {path} from your backup, or delete the "
                          f"damaged file to start with a new server id ({_FREE_WITH}")
        kept = _bad_files(path) if value is None else []
        if kept:
            raise Damaged(f"{path} is missing and {', '.join(kept)} exists from an earlier damaged file. Restore "
                          f"{path} from your backup, or delete {', '.join(kept)} to start with a new server id "
                          f"({_FREE_WITH}")
        if value is None:
            value = os.urandom(SIZE)
            files.write_atomic(path, value.hex().encode() + b"\n", private=True)
        _cache = (path, value)
        return value


def hex_id() -> str:
    return get().hex()


def console_code(key: bytes, server: bytes, address: str, port: int = 443) -> str:
    message = server + socket.inet_aton(address) + port.to_bytes(2, "big")
    return hmac.new(key, message, hashlib.sha256).hexdigest()[:2 * SIZE]
