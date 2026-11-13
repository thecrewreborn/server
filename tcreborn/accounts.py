# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import hashlib
import hmac
import json
import os
import threading
import time

from .saves import save_name

STORE_NAME = "accounts.json"


def _hash(secret: str, salt: bytes) -> str:
    return hashlib.scrypt(secret.encode(), salt=salt, n=2 ** 14, r=8, p=1, dklen=32).hex()


class AccountStore:

    def __init__(self, path: str):
        self.path = path
        self._lock = threading.Lock()
        self._data: dict | None = None
        self._sig: tuple[int, int] | None = None

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
        tmp = self.path + ".tmp"
        try:
            os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
            with open(tmp, "w") as f:
                json.dump(data, f, indent=1)
                f.flush()
                os.fsync(f.fileno())
            st = os.stat(tmp)
            os.replace(tmp, self.path)
        except OSError as e:
            print(f"could not write {self.path}: {e}", flush=True)
            return False
        self._data, self._sig = data, (st.st_mtime_ns, st.st_size)
        return True

    @staticmethod
    def _verify(entry: object, secret: str) -> str:
        try:
            good = hmac.compare_digest(_hash(secret, bytes.fromhex(entry["salt"])), entry["hash"])
        except (KeyError, ValueError, TypeError):
            return "wrong"
        return "ok" if good else "wrong"

    def _entry(self, key: str) -> object:
        with self._lock:
            return self._load().get(key)

    def check(self, name: str, platform: str, secret: str, claim: bool = True) -> str:
        if not secret:
            return "missing"
        key = save_name(name).lower()
        try:
            entry = self._entry(key)
        except OSError:
            return "unavailable"
        if entry is not None:
            return self._verify(entry, secret)
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
        return self._verify(entry, secret)

    def free(self, name: str) -> bool:
        key = save_name(name).lower()
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
        entry = data.get(save_name(name).lower())
        stored = entry.get("name") if isinstance(entry, dict) else None
        return stored if isinstance(stored, str) and stored else None

    def claim_id(self, name: str) -> str | None:
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return None
        entry = data.get(save_name(name).lower())
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
