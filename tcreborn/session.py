# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import json
import os
import time
from dataclasses import dataclass, field

STORE_NAME = "sessions.json"
STORE_MAX_AGE = 2 * 24 * 3600


@dataclass
class Session:
    key: bytes = field(default_factory=lambda: os.urandom(32))
    iv: bytes = field(default_factory=lambda: os.urandom(16))
    connected: bool = False
    answered: set = field(default_factory=set)
    replies: dict = field(default_factory=dict)
    partial: dict = field(default_factory=dict)
    ctx: object = None
    name: str | None = None
    platform: str | None = None
    restored: bool = False
    refused: bool = False
    stamp: float = 0.0
    seen: float = 0.0


class SessionStore:

    def __init__(self, path: str):
        self.path = path

    def load(self) -> dict[tuple[str, int], Session]:
        try:
            with open(self.path) as f:
                entries = json.load(f)
        except (OSError, ValueError):
            return {}
        now, out = time.time(), {}
        for e in entries if isinstance(entries, list) else []:
            try:
                ip, port = e["addr"].rsplit(":", 1)
                if now - float(e["time"]) > STORE_MAX_AGE:
                    continue
                out[(ip, int(port))] = Session(key=bytes.fromhex(e["key"]), iv=bytes.fromhex(e.get("iv", "")),
                                               connected=True, name=e.get("name"), platform=e.get("platform"),
                                               restored=True, stamp=float(e["time"]))
            except (KeyError, ValueError, TypeError, AttributeError):
                continue
        return out

    def save(self, sessions: dict[tuple[str, int], Session]) -> None:
        entries = [{"addr": f"{a[0]}:{a[1]}", "key": s.key.hex(), "iv": s.iv.hex(), "name": s.name,
                    "platform": s.platform, "time": s.stamp}
                   for a, s in sessions.items() if s.connected]
        tmp = self.path + ".tmp"
        try:
            os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
            with open(tmp, "w") as f:
                json.dump(entries, f, indent=1)
                f.flush()
                os.fsync(f.fileno())
            os.replace(tmp, self.path)
        except OSError as e:
            print(f"could not write {self.path}: {e}", flush=True)
