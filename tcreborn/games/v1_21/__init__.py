# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
NAME = "1.21"
PORT_OFFSET = 0


def prepare(start) -> None:
    from tcreborn.games.v1_21 import gamedata
    gamedata.first_start(start.game_data, interactive=start.interactive)


def generate(folder: str) -> int:
    from tcreborn.games.v1_21 import gamedata
    resolved = gamedata.resolve(folder)
    if not resolved:
        raise SystemExit(f"no game files in {folder}")
    return 1 if gamedata.build(resolved) else 0


def make_relay(start):
    from tcreborn import config
    from tcreborn.games.v1_21.relay import Server
    return Server(start.ip, start.port, start.log_path, start.save_path,
                  abilities=config.abilities(), unlocks=config.unlocks(), dlc=config.dlc(),
                  proxy_key=config.proxy_key(), **start.extra)
