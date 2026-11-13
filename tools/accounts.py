# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import argparse
import datetime as dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tcreborn import paths
from tcreborn.accounts import LINK_MINUTES, STORE_NAME, AccountStore


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
    ap = argparse.ArgumentParser(description="List, link or free player-name claims.")
    ap.add_argument("--file", default=paths.home("save", STORE_NAME), help="accounts file (default: %(default)s)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="print the claimed names")
    ln = sub.add_parser("link", help="let one new device log in with a claimed name for a few minutes")
    ln.add_argument("name")
    ln.add_argument("--minutes", type=float, default=LINK_MINUTES, help="how long (default: %(default)s)")
    fr = sub.add_parser("free", help="free a claim")
    fr.add_argument("name")
    args = ap.parse_args(argv)


    store = AccountStore(args.file, owner_of(args.file) if run_as_root() else None)
    if args.cmd == "list":
        for e in store.entries().values():
            name = e['name']
            day = dt.datetime.fromtimestamp(e["created"]).strftime("%Y-%m-%d") if e["created"] else "?"
            devices = f"{e['devices']} device" + ("" if e["devices"] == 1 else "s")
            window = f"  link open until {dt.datetime.fromtimestamp(e['link']):%H:%M}" if e["link"] else ""
            print(f"{name}  {e['platform']}  {day}  {devices}{window}")
    elif args.cmd == "link":
        if not 0 < args.minutes <= 7 * 24 * 60:
            ap.error("--minutes must be more than 0 and at most a week")
        if store.display_name(args.name) is None:
            print(f"no claim for {args.name}")
            return 1
        if not store.open_link(args.name, args.minutes):
            print(f"could not write {args.file}")
            return 1
        print(f"link window for {store.display_name(args.name)} open for {args.minutes:g} minutes: log in with the "
              "new device now")
    elif store.free(args.name):
        print(f"freed {args.name}")
    else:
        print(f"no claim for {args.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
