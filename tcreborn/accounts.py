# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import hashlib
import hmac
import json
import os
import threading
import time

from .files import write_atomic
from .saves import account_key

STORE_NAME = "accounts.json"
LINKS = "@links"
LINK_MINUTES = 10
DEVICES_MAX = 16
VERIFIED_TTL = 600.0
VERIFIED_MAX = 4096
_now = time.monotonic
_wall = time.time


def write_private(path: str, data: object, owner: tuple[int, int] | None = None) -> os.stat_result:
    return write_atomic(path, json.dumps(data, indent=1).encode(), private=True, owner=owner)


HASH_THREADS = min(4, (os.process_cpu_count() if hasattr(os, "process_cpu_count") else os.cpu_count()) or 2)
_HASH_SLOTS = threading.BoundedSemaphore(HASH_THREADS)


def _hash(secret: str, salt: bytes) -> str:
    with _HASH_SLOTS:
        return hashlib.scrypt(secret.encode(), salt=salt, n=2 ** 14, r=8, p=1, dklen=32).hex()


def _normalized(entry: object) -> object:
    if not isinstance(entry, dict) or "credentials" in entry or not isinstance(entry.get("salt"), str):
        return entry
    new = {k: v for k, v in entry.items() if k not in ("salt", "hash")}
    new["id"] = entry["salt"]
    new["credentials"] = [{"salt": entry["salt"], "hash": entry.get("hash"), "platform": entry.get("platform", "?"),
                           "added": entry.get("created", 0)}]
    return new


def _credentials(entry: object) -> list[dict]:
    creds = entry.get("credentials") if isinstance(entry, dict) else None
    return [c for c in creds if isinstance(c, dict)] if isinstance(creds, list) else []


def _links(data: dict) -> dict:
    links = data.get(LINKS)
    return links if isinstance(links, dict) else {}


def _window_open(data: dict, key: str) -> bool:
    until = _links(data).get(key)
    return isinstance(until, (int, float)) and until > _wall()


class AccountStore:

    def __init__(self, path: str, owner: tuple[int, int] | None = None):
        self.path = path
        self.owner = owner
        self._lock = threading.Lock()
        self._data: dict | None = None
        self._sig: tuple[int, int] | None = None


        self._verified: dict[bytes, float] = {}
        self._process_key = os.urandom(32)

    def _read(self) -> tuple[dict, tuple[int, int] | None]:
        try:
            with open(self.path, "rb") as f:
                st = os.fstat(f.fileno())
                raw = f.read()
        except FileNotFoundError:
            return {}, None
        except OSError as e:
            print(f"could not read {self.path}: {e}", flush=True)
            raise
        try:
            data = json.loads(raw.decode())
            if not isinstance(data, dict):
                raise ValueError("not an object")
            return {k: v if k == LINKS else _normalized(v) for k, v in data.items()}, (st.st_mtime_ns, st.st_size)
        except ValueError:
            aside = f"{self.path}.corrupt-{int(time.time())}"
            os.replace(self.path, aside)
            print(f"{self.path} is corrupt: moved to {aside}, starting empty", flush=True)
            return {}, None

    def _load(self) -> dict:
        try:
            st = os.stat(self.path)
            sig = (st.st_mtime_ns, st.st_size)
        except FileNotFoundError:
            sig = None
        if self._data is None or sig != self._sig:
            self._data, self._sig = self._read()
        return self._data

    def _save(self, data: dict) -> bool:
        now = _wall()
        links = {k: v for k, v in _links(data).items()
                 if isinstance(v, (int, float)) and v > now and isinstance(data.get(k), dict)}
        data = {k: v for k, v in data.items() if k != LINKS}
        if links:
            data[LINKS] = links
        try:

            st = write_private(self.path, data, self.owner)
        except OSError as e:
            print(f"could not write {self.path}: {e}", flush=True)
            return False
        self._data, self._sig = data, (st.st_mtime_ns, st.st_size)
        return True

    def _verify(self, key: str, entry: object, secret: str) -> str:
        tries = []
        for cred in _credentials(entry):
            salt = cred.get("salt")
            if isinstance(salt, str):
                tries.append((cred, salt, self._mark(key, salt, secret)))
        now = _now()
        with self._lock:
            for _, _, mark in tries:
                when = self._verified.get(mark)
                if when is not None and now - when < VERIFIED_TTL:
                    return "ok"
        for cred, salt, mark in tries:
            try:
                good = hmac.compare_digest(_hash(secret, bytes.fromhex(salt)), cred["hash"])
            except (KeyError, ValueError, TypeError):
                continue
            if good:
                break
        else:
            return "wrong"
        with self._lock:
            self._remember(mark, now)
        return "ok"

    def _mark(self, key: str, salt: str, secret: str) -> bytes:
        return hmac.new(self._process_key, "\0".join((key, salt, secret)).encode(), hashlib.sha256).digest()

    def _remember(self, mark: bytes, now: float) -> None:
        self._verified.pop(mark, None)
        self._verified[mark] = now
        while self._verified and (len(self._verified) > VERIFIED_MAX
                                  or now - next(iter(self._verified.values())) >= VERIFIED_TTL):
            del self._verified[next(iter(self._verified))]

    def _linked_meanwhile(self, key: str, entry: dict, current: dict, secret: str) -> bool:
        known = {c.get("salt") for c in _credentials(entry)}
        now = _now()
        for cred in _credentials(current):
            salt = cred.get("salt")
            if isinstance(salt, str) and salt not in known:
                when = self._verified.get(self._mark(key, salt, secret))
                if when is not None and now - when < VERIFIED_TTL:
                    return True
        return False

    def _may_link(self, key: str, entry: dict, data: dict, secret: str, link: str) -> str | None:
        current = data.get(key)
        if not isinstance(current, dict) or current.get("id") != entry["id"]:
            return "wrong"
        if self._linked_meanwhile(key, entry, current, secret):
            return "ok"
        if link == "window" and not _window_open(data, key):
            return "wrong"
        if len(_credentials(current)) >= DEVICES_MAX:
            return "full"
        return None

    def _entry(self, key: str) -> object:
        with self._lock:
            return self._load().get(key)

    def _link(self, key: str, entry: object, platform: str, secret: str, link: str) -> str:
        if link not in ("always", "window") or not isinstance(entry, dict) or not isinstance(entry.get("id"), str):
            return "wrong"
        with self._lock:
            try:
                verdict = self._may_link(key, entry, self._load(), secret, link)
            except OSError:
                return "unavailable"
        if verdict is not None:
            return verdict
        salt = os.urandom(16).hex()
        hashed = _hash(secret, bytes.fromhex(salt))
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return "unavailable"
            verdict = self._may_link(key, entry, data, secret, link)
            if verdict is not None:
                return verdict
            current = data[key]
            new = dict(data)
            new[key] = dict(current, credentials=_credentials(current) + [
                {"salt": salt, "hash": hashed, "platform": platform, "added": int(time.time())}])
            new[LINKS] = {k: v for k, v in _links(data).items() if k != key}
            if not self._save(new):
                return "unavailable"
            self._remember(self._mark(key, salt, secret), _now())
            return "linked"

    def check(self, name: str, platform: str, secret: str, claim: bool = True, link: str = "never") -> str:
        if not secret.strip():
            return "missing"
        key = account_key(name)
        try:
            entry = self._entry(key)
        except OSError:
            return "unavailable"
        if entry is None:
            if not claim:
                return "unclaimed"
            salt = os.urandom(16)
            hashed = _hash(secret, salt)
            with self._lock:
                try:
                    data = self._load()
                except OSError:
                    return "unavailable"
                entry = data.get(key)
                if entry is None:
                    new = dict(data)
                    now = int(time.time())
                    new[key] = {"name": name, "platform": platform, "created": now, "id": os.urandom(16).hex(),
                                "credentials": [{"salt": salt.hex(), "hash": hashed, "platform": platform,
                                                 "added": now}]}
                    return "claimed" if self._save(new) else "unavailable"

        verdict = self._verify(key, entry, secret)
        return verdict if verdict == "ok" else self._link(key, entry, platform, secret, link)

    def open_link(self, name: str, minutes: float = LINK_MINUTES) -> bool:
        key = account_key(name)
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return False
            if not isinstance(data.get(key), dict) or minutes <= 0:
                return False
            new = dict(data)
            new[LINKS] = dict(_links(data), **{key: int(_wall() + minutes * 60)})
            return self._save(new)

    def free(self, name: str) -> bool:
        key = account_key(name)
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return False
            if key not in data:
                return False
            return self._save({k: v for k, v in data.items() if k != key})

    def display_name(self, name: str) -> str | None:
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return None
        entry = data.get(account_key(name))
        stored = entry.get("name") if isinstance(entry, dict) else None
        return stored if isinstance(stored, str) and stored else None

    def account_id(self, name: str) -> str | None:
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return None
        entry = data.get(account_key(name))
        ident = entry.get("id") if isinstance(entry, dict) else None
        return ident if isinstance(ident, str) and ident else None

    def names(self) -> list[str]:
        return sorted(v["name"] for v in self.entries().values())

    def entries(self) -> dict:
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return {}
            return {k: {"name": v.get("name", k), "platform": v.get("platform", "?"), "created": v.get("created", 0),
                        "devices": len(_credentials(v)),
                        "link": _links(data)[k] if _window_open(data, k) else None}
                    for k, v in sorted(data.items()) if k != LINKS and isinstance(v, dict)}
