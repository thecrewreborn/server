# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import configparser
import errno
import os
import shutil
import socket
import sys
import threading

from tcreborn import config, games, paths
from tcreborn.version import VERSION

ROOT = paths.HOME
FALLBACK_PORTS = {80: 8080, 443: 8443}


def _first_config() -> None:
    target = os.environ.get("TCREBORN_CONFIG") or paths.home("config.ini")
    example = os.path.join(os.path.dirname(paths.PACKAGE), "config.example.ini")
    if not os.path.exists(target) and os.path.exists(example):
        shutil.copyfile(example, target)
        print(f"created {target}", flush=True)


def _udp_in_use(*ports: int) -> int | None:
    for port in ports:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.bind(("0.0.0.0", port))
        except OSError:
            return port
        finally:
            probe.close()
    return None


def _bind(serve, port: int, name: str, *args):
    try:
        return serve(port, *args), port
    except OSError as e:
        alt = FALLBACK_PORTS.get(port)
        if alt is None:
            raise SystemExit(f"{name}: cannot open tcp port {port}: {e.strerror}")
        why = "is in use by another program" if e.errno in (errno.EADDRINUSE, 10048) else "needs more rights"
        hint = (" (Linux: ports below 1024 need root, or once: sudo setcap cap_net_bind_service=+ep "
                f"{os.path.realpath(sys.executable)})" if sys.platform.startswith("linux") and port < 1024 else "")
        print(f"{name}: tcp port {port} {why}{hint}; using {alt}. The game learns the port from discovery; a "
              f"server.txt / reborn.ini with a fixed address needs :{alt}.", flush=True)
        return serve(alt, *args), alt


def _check_server_ip() -> None:
    problem = config.server_ip_problem()
    if problem:
        print(f"ERROR: {problem}", flush=True)


def _check_config() -> None:
    try:
        problem = config.public_problem()
    except configparser.Error as e:
        raise SystemExit(f"ERROR: config.ini cannot be read: {e}\nOpen config.ini and change the existing lines; each "
                         f"[section] heading and each key may appear only once.")
    if problem:
        raise SystemExit(f"ERROR: {problem}. The server does not start.")


def _say_mode() -> None:
    public = config.public()
    print(f"public mode: {'on' if public else 'off'}", flush=True)
    if public and config.advertised_ip() is None:
        print("WARNING: public mode without [server] ip: the server announces the address it is reached at in its own "
              "network. Behind NAT (a cloud VM, a router at home) players on the internet cannot reach that address; "
              "set [server] ip to the public IPv4 address (README: Running a public server).", flush=True)


def _public_mode(ubi, versions) -> None:
    from tcreborn.accounts import STORE_NAME, AccountStore
    from tcreborn.saves import DEFAULT_NAME, account_key, save_name
    ubi.ACCOUNTS = AccountStore(paths.home("save", STORE_NAME))
    print("names are claimed with a password (PC) or console key (PS4)", flush=True)
    claimed = {account_key(n) for n in ubi.ACCOUNTS.names()} | {account_key(DEFAULT_NAME)}
    unclaimed = set()
    for game in versions:
        folder = paths.home("save", game.NAME, "players")
        if os.path.isdir(folder):

            unclaimed |= {f[:-5] for f in os.listdir(folder) if f.endswith(".json") and save_name(f[:-5]) == f[:-5]
                          and os.path.isfile(os.path.join(folder, f))}
    unclaimed = {n for n in unclaimed if account_key(n) not in claimed}
    if unclaimed:
        print(f"unclaimed saves (anyone may claim them at the next login): {', '.join(sorted(unclaimed))}", flush=True)


def _check_server_id() -> None:
    from tcreborn import server_id
    try:
        server_id.get()
    except server_id.Damaged as e:
        raise SystemExit(f"ERROR: {e}")
    except OSError as e:
        raise SystemExit(f"ERROR: the server id {server_id.PATH} cannot be written: {e}. The server does not start.")


def main(argv: list[str] | None = None) -> None:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    _first_config()
    _check_config()
    ap = argparse.ArgumentParser(prog="tcreborn")
    relay_ip, relay_port = config.relay_address()
    ap.add_argument("--version", action="version", version=f"The Crew Reborn server {VERSION}")
    ap.add_argument("--ip", default=relay_ip)
    ap.add_argument("--port", type=int, default=relay_port)
    ap.add_argument("--http-port", type=int, default=config.http_port())
    ap.add_argument("--log", default=os.path.join(ROOT, "captures", "proxy.jsonl"))
    ap.add_argument("--save", default=config.save_path(), help="player save file (JSON)")
    ap.add_argument("--verbose", "-v", action="store_true", help="show ports and settings at the start")
    ap.add_argument("--generate", metavar="GAME_FOLDER",
                    help="build the game tables from your game files (PC data_win32 or PS4 app0/patch0) and exit")
    args = ap.parse_args(argv)
    print(f"The Crew Reborn server {VERSION}", flush=True)
    _say_mode()
    versions = games.available()
    if args.generate:
        builders = [g for g in versions if hasattr(g, "generate")]
        if not builders:
            raise SystemExit("this server builds no game tables")
        raise SystemExit(max(g.generate(args.generate) for g in builders))
    start = games.Start(args.ip, args.port, os.path.dirname(args.log), paths.home("save"), args.save,
                        log_path=args.log, game_data=config.game_data(),
                        interactive=sys.stdin is not None and sys.stdin.isatty())
    for game in versions:
        game.prepare(start)


    from tcreborn import discovery, saves, tls, ubi

    taken = _udp_in_use(*sorted({args.port + g.PORT_OFFSET for g in versions}), discovery.DISCOVERY_PORT)
    if taken:
        raise SystemExit(f"\nThe Crew Reborn server is already running on this computer (udp port {taken} is in use).\n"
                         f"Use that one, or close its window first.")
    _check_server_id()
    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    if not config.save_path_configured() and os.path.abspath(args.save) == config.save_path():
        saves.move_into_version_folders()
    detail = print if args.verbose else (lambda *a, **k: None)
    detail(f"player save: {args.save}")
    detail(f"abilities: {config.abilities()}, unlocks: {config.unlocks()}, dlc: {config.dlc()}")
    print(f"DLC: {'on' if config.dlc() == 'all' else 'off'} (config.ini [player] dlc)", flush=True)
    _check_server_ip()
    if config.public():
        _public_mode(ubi, versions)
    requests_log = os.path.join(os.path.dirname(args.log), "requests.jsonl")
    _, http_port = _bind(ubi.serve, args.http_port, "http", requests_log)
    ubi.HTTP_PORT = http_port
    https_port = config.https_port()
    if https_port:
        if tls.ensure(*config.tls_cert(), config.advertised_ip()):
            print(f"created the HTTPS certificate {config.tls_cert()[0]}", flush=True)
        _, https_port = _bind(ubi.serve_tls, https_port, "https", *config.tls_cert(), requests_log)
        detail(f"https on tcp 0.0.0.0:{https_port}", flush=True)
    discovery.serve(args.port, http_port, config.advertised_ip(), bots=config.bots(), afk=config.afk_kick(),
                    https_port=https_port, dlc=config.dlc() == "all")
    detail(f"http on tcp 0.0.0.0:{http_port}, discovery on udp 0.0.0.0:{discovery.DISCOVERY_PORT}, "
           f"relay on udp {args.ip}:{args.port}, freeride bots {'on' if config.bots() else 'off'}, "
           f"idle kick {'on' if config.afk_kick() else 'off'}", flush=True)
    relays = [versions[0].make_relay(start)]
    for game in versions[1:]:
        try:
            relays.append(game.make_relay(start))
        except Exception as e:
            print(f"game version {game.NAME} not started: {type(e).__name__}: {e}", flush=True)
    print(f"\nServer address: {server_address(config.server_ip(), https_port)}\n"
          f"Ready. Start The Crew on your PS4 or PC; the game finds the server on the local network by itself.\n"
          f"Stop the server with Ctrl+C.\n", flush=True)
    for beside in relays[1:]:
        threading.Thread(target=beside.serve_forever, daemon=True).start()
    relays[0].serve_forever()


def server_address(ip: str, https_port: int) -> str:
    return f"{ip}:{https_port}" if https_port and https_port != 443 else ip


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nServer stopped.", flush=True)
