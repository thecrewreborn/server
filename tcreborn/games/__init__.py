# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import importlib


BESIDE_1_21 = ("v1_00",)


def start_beside(relay_ip: str, relay_port: int, log_dir: str, save_dir: str) -> list[str]:
    started = []
    for name in BESIDE_1_21:
        try:
            relay = importlib.import_module(f"tcreborn.games.{name}.relay")
        except ModuleNotFoundError:
            continue
        relay.serve(relay_ip, relay_port, log_dir, save_dir)
        started.append(name)
    return started
