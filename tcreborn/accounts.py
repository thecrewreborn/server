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
VERIFIED_TTL = 600.0
VERIFIED_MAX = 4096
_now = time.monotonic


def write_private(path: str, data: object, owner: tuple[int, int] | None = None) -> os.stat_result:
    return write_atomic(path, json.dumps(data, indent=1).encode(), private=True, owner=owner)


HASH_THREADS = min(4, (os.process_cpu_count() if hasattr(os, "process_cpu_count") else os.cpu_count()) or 2)
_HASH_SLOTS = threading.BoundedSemaphore(HASH_THREADS)


def _hash(secret: str, salt: bytes) -> str:
    with _HASH_SLOTS:
        return hashlib.scrypt(secret.encode(), salt=salt, n=2 ** 14, r=8, p=1, dklen=32).hex()


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
            return data, (st.st_mtime_ns, st.st_size)
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
        try:

            st = write_private(self.path, data, self.owner)
        except OSError as e:
            print(f"could not write {self.path}: {e}", flush=True)
            return False
        self._data, self._sig = data, (st.st_mtime_ns, st.st_size)
        return True

    def _verify(self, key: str, entry: object, secret: str) -> str:
        try:
            salt = entry["salt"]
            mark = hmac.new(self._process_key, "\0".join((key, salt, secret)).encode(), hashlib.sha256).digest()
        except (KeyError, TypeError):
            return "wrong"
        now = _now()
        with self._lock:
            when = self._verified.get(mark)
        if when is not None and now - when < VERIFIED_TTL:
            return "ok"
        try:
            good = hmac.compare_digest(_hash(secret, bytes.fromhex(salt)), entry["hash"])
        except (KeyError, ValueError, TypeError):
            return "wrong"
        if not good:
            return "wrong"
        with self._lock:
            self._verified.pop(mark, None)
            self._verified[mark] = now
            while self._verified and (len(self._verified) > VERIFIED_MAX
                                      or now - next(iter(self._verified.values())) >= VERIFIED_TTL):
                del self._verified[next(iter(self._verified))]
        return "ok"

    def _entry(self, key: str) -> object:
        with self._lock:
            return self._load().get(key)

    def check(self, name: str, platform: str, secret: str, claim: bool = True) -> str:
        if not secret.strip():
            return "missing"
        key = account_key(name)
        try:
            entry = self._entry(key)
        except OSError:
            return "unavailable"
        if entry is not None:
            return self._verify(key, entry, secret)
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
                new[key] = {"name": name, "platform": platform, "salt": salt.hex(), "hash": hashed,
                            "created": int(time.time())}
                return "claimed" if self._save(new) else "unavailable"
        return self._verify(key, entry, secret)

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

    def claim_id(self, name: str) -> str | None:
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return None
        entry = data.get(account_key(name))
        salt = entry.get("salt") if isinstance(entry, dict) else None
        return salt if isinstance(salt, str) and salt else None

    def names(self) -> list[str]:
        return sorted(v["name"] for v in self.entries().values())

    def entries(self) -> dict:
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return {}
            return {k: {"name": v.get("name", k), "platform": v.get("platform", "?"), "created": v.get("created", 0)}
                    for k, v in sorted(data.items()) if isinstance(v, dict)}
