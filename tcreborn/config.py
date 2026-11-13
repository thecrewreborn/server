# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import configparser
import ipaddress
import os
import socket

from tcreborn import paths

ROOT = paths.HOME
GAME_RELAY = ("185.38.20.88", 4000)
RELAY = ("0.0.0.0", 4000)


_cached: tuple[str, object, configparser.ConfigParser] | None = None


def _load() -> configparser.ConfigParser:
    global _cached
    path = os.environ.get("TCREBORN_CONFIG", os.path.join(ROOT, "config.ini"))
    try:
        st = os.stat(path)
        sig: object = (st.st_mtime_ns, st.st_size, st.st_ino)
    except OSError:
        sig = None
    cached = _cached
    if cached is not None and cached[0] == path and cached[1] == sig:
        return cached[2]
    cfg = configparser.ConfigParser()
    cfg.read(path)
    _cached = (path, sig, cfg)
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


def _server_ip_setting() -> str:
    return _load().get("server", "ip", fallback="").strip()


def _is_ipv4(value: str) -> bool:
    try:
        ipaddress.IPv4Address(value)
    except ValueError:
        return False
    return True


def advertised_ip() -> str | None:
    value = _server_ip_setting()
    return value if value and _is_ipv4(value) else None


def server_ip_problem() -> str | None:
    value = _server_ip_setting()
    if not value or _is_ipv4(value):
        return None
    return (f"config.ini [server] ip = {value} is not an IPv4 address (like 203.0.113.5); it is ignored and the "
            f"server announces the address the game reaches it at")


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


def crew_credits_refill() -> bool:
    return _load().get("player", "crew_credits_refill", fallback="on").strip().lower() in ("on", "1", "true", "yes")


def dlc() -> str:
    value = _load().get("player", "dlc", fallback="off").strip().lower()
    return "all" if value in ("on", "1", "true", "yes", "all") else "none"


def unlocks() -> str:
    return _load().get("player", "unlocks", fallback="none")


def bots() -> bool:
    return _load().get("world", "bots", fallback="off").strip().lower() in ("on", "1", "true", "yes")


def multiplayer() -> bool:
    cfg = _load()
    if not cfg.has_option("world", "multiplayer"):
        from tcreborn import ubi
        return ubi.ACCOUNTS is None
    return cfg.get("world", "multiplayer").strip().lower() in ("on", "1", "true", "yes")


def afk_kick() -> bool:
    return _load().get("world", "afk_kick", fallback="off").strip().lower() in ("on", "1", "true", "yes")


SWITCH = {"on": True, "1": True, "true": True, "yes": True, "off": False, "0": False, "false": False, "no": False,
          "": False}


def _public_setting() -> str:
    return _load().get("server", "public", fallback="off").strip()


def public() -> bool:
    return SWITCH.get(_public_setting().lower(), False)


def public_problem() -> str | None:
    value = _public_setting()
    if value.lower() in SWITCH:
        return None
    return f"config.ini [server] public = {value} is not on or off (also allowed: yes/no, true/false, 1/0)"


def proxy_key() -> tuple[bytes, bytes]:
    from tcreborn.keys import IV0, K0
    cfg = _load()
    return (bytes.fromhex(cfg.get("proxy", "key", fallback="")) or K0,
            bytes.fromhex(cfg.get("proxy", "iv", fallback="")) or IV0)
