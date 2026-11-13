# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import datetime
import os
import re
import shutil

from tcreborn import paths

FOLDER_1_21 = "1.21"
OLD_1_21 = ("players", "player.json", "factions.json")


DEFAULT_NAME = "Player"


def save_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_-]", "_", name)[:32] or DEFAULT_NAME


def account_key(name: str) -> str:
    return save_name(name).lower()


def move_into_version_folders(save_root: str | None = None) -> list[str]:
    root = save_root or paths.home("save")
    target = os.path.join(root, FOLDER_1_21)

    old = [n for n in OLD_1_21 if os.path.exists(os.path.join(root, n)) and not os.path.exists(os.path.join(target, n))]
    if not old:
        return []
    backup = os.path.join(root, "backup", "before-version-folders-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    os.makedirs(backup)
    for n in old:
        src = os.path.join(root, n)
        (shutil.copytree if os.path.isdir(src) else shutil.copy2)(src, os.path.join(backup, n))
    os.makedirs(target, exist_ok=True)
    for n in old:
        shutil.move(os.path.join(root, n), os.path.join(target, n))
    print(f"Moved the saves into {target} (copy in {backup})", flush=True)
    return old
