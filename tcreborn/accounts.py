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

    def _load(self) -> dict:
        try:
            with open(self.path, "rb") as f:
                raw = f.read()
        except FileNotFoundError:
            return {}
        except OSError as e:
            print(f"could not read {self.path}: {e}", flush=True)
            raise
        try:
            data = json.loads(raw.decode())
            if not isinstance(data, dict):
                raise ValueError("not an object")
            return data
        except ValueError:
            aside = f"{self.path}.corrupt-{int(time.time())}"
            os.replace(self.path, aside)
            print(f"{self.path} is corrupt: moved to {aside}, starting empty", flush=True)
            return {}

    def _save(self, data: dict) -> bool:
        tmp = self.path + ".tmp"
        try:
            os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
            with open(tmp, "w") as f:
                json.dump(data, f, indent=1)
            os.replace(tmp, self.path)
            return True
        except OSError as e:
            print(f"could not write {self.path}: {e}", flush=True)
            return False

    def check(self, name: str, platform: str, secret: str) -> str:
        if not secret:
            return "missing"
        key = save_name(name)
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return "unavailable"
            entry = data.get(key)
            if entry is None:
                salt = os.urandom(16)
                data[key] = {"platform": platform, "salt": salt.hex(), "hash": _hash(secret, salt),
                             "created": int(time.time())}
                return "claimed" if self._save(data) else "unavailable"
            try:
                good = hmac.compare_digest(_hash(secret, bytes.fromhex(entry["salt"])), entry["hash"])
            except (KeyError, ValueError, TypeError):
                return "wrong"
            return "ok" if good else "wrong"

    def free(self, name: str) -> bool:
        key = save_name(name)
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return False
            if key not in data:
                return False
            del data[key]
            return self._save(data)

    def names(self) -> list[str]:
        return sorted(self.entries())

    def entries(self) -> dict:
        with self._lock:
            try:
                data = self._load()
            except OSError:
                return {}
            return {k: {"platform": v.get("platform", "?"), "created": v.get("created", 0)}
                    for k, v in sorted(data.items()) if isinstance(v, dict)}
