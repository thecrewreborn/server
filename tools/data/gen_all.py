# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TABLES = [("gen_summit.py", "summit.json"), ("gen_regions.py", "regions.json"), ("gen_collectibles.py", "collectibles.json"),
          ("gen_cars.py", "cars.json"), ("gen_unlocks.py", "dlc.json"), ("gen_rewards.py", "rewards.json"),
          ("gen_npc.py", "npc.json")]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "..", "..", "tcreborn", "data"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    failed = 0
    for script, table in TABLES:
        r = subprocess.run([sys.executable, os.path.join(HERE, script), args.data_dir, "-o", os.path.join(args.out, table)])
        failed += r.returncode != 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
