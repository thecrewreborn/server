# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import configparser
import os
import socket

from tcreborn import paths

ROOT = paths.HOME
GAME_RELAY = ("185.38.20.88", 4000)
RELAY = ("0.0.0.0", 4000)


def _load() -> configparser.ConfigParser:
    cfg = configparser.ConfigParser()
    cfg.read(os.environ.get("TCREBORN_CONFIG", os.path.join(ROOT, "config.ini")))
    return cfg


def lan_ip() -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("10.255.255.255", 1))
        except OSError:
            return "127.0.0.1"
        return s.getsockname()[0]


def server_ip() -> str:
    return advertised_ip() or lan_ip()


def advertised_ip() -> str | None:
    return _load().get("server", "ip", fallback="") or None


def http_port() -> int:
    return _load().getint("server", "http_port", fallback=80)


def https_port() -> int:
    return _load().getint("server", "https_port", fallback=443)


def tls_cert() -> tuple[str, str]:
    cfg = _load()
    return (os.path.join(ROOT, cfg.get("server", "tls_cert", fallback="certs/server.crt")),
            os.path.join(ROOT, cfg.get("server", "tls_key", fallback="certs/server.key")))


def ps4_ip() -> str | None:
    return _load().get("ps4", "ip", fallback="") or None


def relay_address() -> tuple[str, int]:
    cfg = _load()
    return cfg.get("relay", "ip", fallback=RELAY[0]), cfg.getint("relay", "port", fallback=RELAY[1])


OLD_SAVE_PATH = "save/player.json"


def _configured_save_path() -> str:
    path = _load().get("server", "save_path", fallback="").strip()
    return "" if path.replace("\\", "/") in ("", OLD_SAVE_PATH) else path


def save_path_configured() -> bool:
    return bool(_configured_save_path())


def save_path() -> str:
    path = _configured_save_path() or os.path.join("save", "1.21", "player.json")
    return path if os.path.isabs(path) else os.path.join(ROOT, path)


def game_data() -> str | None:
    path = _load().get("server", "game_data", fallback="").strip()
    return (path if os.path.isabs(path) else os.path.join(ROOT, path)) if path else None


def abilities() -> str:
    return _load().get("player", "abilities", fallback="none")


def crew_credits() -> int:
    return max(0, _load().getint("player", "crew_credits", fallback=1000000))


def dlc() -> str:
    return "all"


def unlocks() -> str:
    return _load().get("player", "unlocks", fallback="none")


def bots() -> bool:
    return _load().get("world", "bots", fallback="off").strip().lower() in ("on", "1", "true", "yes")


def multiplayer() -> bool:
    return _load().get("world", "multiplayer", fallback="on").strip().lower() in ("on", "1", "true", "yes")


def afk_kick() -> bool:
    return _load().get("world", "afk_kick", fallback="off").strip().lower() in ("on", "1", "true", "yes")


def proxy_key() -> tuple[bytes, bytes]:
    from tcreborn.keys import IV0, K0
    cfg = _load()
    return (bytes.fromhex(cfg.get("proxy", "key", fallback="")) or K0,
            bytes.fromhex(cfg.get("proxy", "iv", fallback="")) or IV0)
