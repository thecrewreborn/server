# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import os
import runpy
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
TABLES = [("gen_summit.py", "summit.json"), ("gen_regions.py", "regions.json"), ("gen_collectibles.py", "collectibles.json"),
          ("gen_cars.py", "cars.json"), ("gen_unlocks.py", "dlc.json"), ("gen_rewards.py", "rewards.json"),
          ("gen_npc.py", "npc.json"), ("gen_physparts.py", "physparts.json")]


def run(script: str, args: list[str]) -> bool:
    path = os.path.join(HERE, script)
    saved = sys.argv, list(sys.path)
    sys.argv = [path, *args]
    try:
        runpy.run_path(path, run_name="__main__")
        return True
    except SystemExit as e:
        if e.code not in (None, 0):
            print(f"{script}: {e.code}", file=sys.stderr)
        return e.code in (None, 0)
    except Exception:
        traceback.print_exc()
        return False
    finally:
        sys.argv, sys.path[:] = saved


def generate(data_dir: str, out: str) -> list[str]:
    os.makedirs(out, exist_ok=True)
    failed = []
    for script, table in TABLES:
        target = os.path.join(out, table)
        if run(script, [data_dir, "-o", target + ".new"]) and os.path.exists(target + ".new"):
            os.replace(target + ".new", target)
        else:
            failed.append(table)
            if os.path.exists(target + ".new"):
                os.remove(target + ".new")
    return failed


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "..", "..", "tcreborn", "games", "v1_21", "data"))
    args = ap.parse_args()
    failed = generate(args.data_dir, args.out)
    if failed:
        print(f"failed: {', '.join(failed)}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
