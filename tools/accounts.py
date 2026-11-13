# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import datetime as dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tcreborn import paths
from tcreborn.accounts import STORE_NAME, AccountStore


def owner_of(path: str) -> tuple[int, int] | None:
    for p in (path, os.path.dirname(os.path.abspath(path))):
        try:
            st = os.lstat(p)
        except FileNotFoundError:
            continue
        if p == path and os.path.islink(p):
            continue
        return st.st_uid, st.st_gid
    return None


def run_as_root() -> bool:
    return hasattr(os, "geteuid") and os.geteuid() == 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="List or free player-name claims.")
    ap.add_argument("--file", default=paths.home("save", STORE_NAME), help="accounts file (default: %(default)s)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="print the claimed names")
    fr = sub.add_parser("free", help="free a claim")
    fr.add_argument("name")
    args = ap.parse_args(argv)


    store = AccountStore(args.file, owner_of(args.file) if run_as_root() else None)
    if args.cmd == "list":
        for e in store.entries().values():
            name = e['name']
            day = dt.datetime.fromtimestamp(e["created"]).strftime("%Y-%m-%d") if e["created"] else "?"
            print(f"{name}  {e['platform']}  {day}")
    elif store.free(args.name):
        print(f"freed {args.name}")
    else:
        print(f"no claim for {args.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
