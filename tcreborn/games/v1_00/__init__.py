# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os

NAME = "1.00"
PORT_OFFSET = 100


def prepare(start) -> None:
    folder, old = start.save_folder(NAME), start.save_folder("v100")
    if not os.path.exists(folder) and os.path.isdir(old):
        os.rename(old, folder)
    os.makedirs(folder, exist_ok=True)


def make_relay(start):
    from tcreborn.games.v1_00.relay import V100Server
    folder = start.save_folder(NAME)
    print(f"1.00 relay on udp {start.ip}:{start.port + PORT_OFFSET}, saves in {folder}", flush=True)
    return V100Server(start.ip, start.port + PORT_OFFSET, os.path.join(start.log_dir, "v100.jsonl"),
                      os.path.join(folder, "player.json"))
