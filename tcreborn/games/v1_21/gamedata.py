# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os
import sys

from tcreborn import paths
from tcreborn.games.v1_21 import tables

INSTALLS = [
    r"Program Files (x86)\Ubisoft\Ubisoft Game Launcher\games\The Crew",
    r"Program Files\Ubisoft\Ubisoft Game Launcher\games\The Crew",
    r"Program Files (x86)\Steam\steamapps\common\The Crew",
    r"Program Files\Steam\steamapps\common\The Crew",
    r"SteamLibrary\steamapps\common\The Crew",
    r"Games\The Crew",
    r"The Crew",
]


def _gen_all():
    if paths.TOOLS not in sys.path:
        sys.path.insert(0, paths.TOOLS)
    import gen_all
    return gen_all


def names() -> list[str]:
    return [table for _, table in _gen_all().TABLES]


def missing() -> list[str]:
    return [n for n in names() if not os.path.exists(tables.path(n))]


def _has_bigfiles(d: str) -> bool:
    try:
        return any(n.endswith(".fat") for n in os.listdir(d))
    except OSError:
        return False


def resolve(folder: str) -> str | None:
    folder = folder.strip().strip('"').strip("'").strip()
    if not folder:
        return None
    folder = os.path.abspath(os.path.expanduser(folder.replace("\\ ", " ") if os.sep == "/" else folder))
    if os.path.isfile(folder):
        folder = os.path.dirname(folder)
    if os.path.basename(folder).lower() in ("app0", "patch0") and _has_bigfiles(folder):
        parent = os.path.dirname(folder)
        return parent if os.path.isdir(os.path.join(parent, "app0")) else folder
    for d in (folder, os.path.join(folder, "data_win32"), os.path.join(folder, "The Crew", "data_win32")):
        if _has_bigfiles(d) or _has_bigfiles(os.path.join(d, "app0")):
            return d
    return None


def find_install() -> str | None:
    candidates = [paths.HOME, os.path.join(paths.HOME, "The Crew")]
    if os.name == "nt":
        candidates += [f"{drive}:\\{p}" for drive in "CDEFGH" for p in INSTALLS]
    for c in candidates:
        found = resolve(c)
        if found:
            return found
    return None


def build(folder: str) -> list[str]:
    print(f"Building the game tables from {folder} (takes about a minute)...", flush=True)
    failed = _gen_all().generate(folder, tables.FOLDER)
    if failed:
        print(f"Could not build: {', '.join(failed)}", flush=True)
    else:
        print(f"Game tables ready in {tables.FOLDER}", flush=True)
    return failed


def _ask() -> str | None:
    print("\nThe server needs a few tables from your own copy of The Crew (cars, rewards, summits).\n"
          "  PC:  the game folder, e.g. C:\\Program Files (x86)\\Ubisoft\\Ubisoft Game Launcher\\games\\The Crew\n"
          "  PS4: a folder with app0 (and patch0) copied from the console\n"
          "Drag the folder into this window or type its path, then press Enter.\n"
          "Press Enter alone to start without the tables (missions pay nothing, no car dealer, no summits).",
          flush=True)
    while True:
        try:
            answer = input("> ")
        except EOFError:
            return None
        if not answer.strip():
            return None
        found = resolve(answer)
        if found:
            return found
        print("No game files in that folder (looking for .fat files in it, in data_win32 or in app0). Try again, "
              "or press Enter to skip.", flush=True)


def first_start(configured: str | None, interactive: bool) -> None:
    gone = missing()
    if not gone:
        return
    folder = resolve(configured) if configured else None
    if configured and not folder:
        print(f"[server] game_data = {configured}: no game files there", flush=True)
    folder = folder or find_install()
    if folder is None and interactive:
        folder = _ask()
    if folder is None:
        print(f"Game tables missing ({', '.join(gone)}): set game_data in config.ini or start once with "
              f"--generate <game folder>. Running without them.", flush=True)
        return
    build(folder)
