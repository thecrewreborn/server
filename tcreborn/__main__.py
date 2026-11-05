# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import os

from tcreborn import config, discovery, tls, ubi
from tcreborn.server import Server
from tcreborn.version import VERSION

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="tcreborn")
    relay_ip, relay_port = config.relay_address()
    ap.add_argument("--version", action="version", version=f"The Crew Reborn server {VERSION}")
    ap.add_argument("--ip", default=relay_ip)
    ap.add_argument("--port", type=int, default=relay_port)
    ap.add_argument("--http-port", type=int, default=config.http_port())
    ap.add_argument("--log", default=os.path.join(ROOT, "captures", "proxy.jsonl"))
    ap.add_argument("--save", default=config.save_path(), help="player save file (JSON)")
    args = ap.parse_args(argv)
    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    print(f"The Crew Reborn server {VERSION}", flush=True)
    print(f"player save: {args.save}")
    print(f"abilities: {config.abilities()}, unlocks: {config.unlocks()}, dlc: {config.dlc()}")
    requests_log = os.path.join(os.path.dirname(args.log), "requests.jsonl")
    ubi.serve(args.http_port, requests_log)
    if config.https_port():
        if tls.ensure(*config.tls_cert(), config.advertised_ip()):
            print(f"created the HTTPS certificate {config.tls_cert()[0]}", flush=True)
        ubi.serve_tls(config.https_port(), *config.tls_cert(), requests_log)
        print(f"https on tcp 0.0.0.0:{config.https_port()} (PC game)", flush=True)
    discovery.serve(args.port, args.http_port, config.advertised_ip(), bots=config.bots(), afk=config.afk_kick(),
                    https_port=config.https_port())
    print(f"http on tcp 0.0.0.0:{args.http_port}, discovery on udp 0.0.0.0:{discovery.DISCOVERY_PORT}, "
          f"freeride bots {'on' if config.bots() else 'off'}, idle kick {'on' if config.afk_kick() else 'off'}",
          flush=True)
    Server(args.ip, args.port, args.log, args.save,
           abilities=config.abilities(), unlocks=config.unlocks(), dlc=config.dlc(),
           proxy_key=config.proxy_key()).serve_forever()


if __name__ == "__main__":
    main()
