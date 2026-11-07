# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import importlib
import os
from dataclasses import dataclass, field

VERSIONS = ("v1_21", "v1_00")


@dataclass
class Start:
    ip: str
    port: int
    log_dir: str
    save_root: str
    save_path: str
    log_path: str = ""
    game_data: str | None = None
    interactive: bool = False
    extra: dict = field(default_factory=dict)

    def save_folder(self, name: str) -> str:
        return os.path.join(self.save_root, name)


def available() -> list:
    found = []
    for name in VERSIONS:
        try:
            found.append(importlib.import_module(f"tcreborn.games.{name}"))
        except ModuleNotFoundError:
            continue
    return found
