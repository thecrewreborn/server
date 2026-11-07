# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os

PACKAGE = os.path.dirname(os.path.abspath(__file__))
HOME = os.environ.get("TCREBORN_HOME") or os.path.dirname(PACKAGE)
BUNDLED = os.path.join(PACKAGE, "data")
TABLES = os.path.join(PACKAGE, "games", "v1_21", "data")
TOOLS = os.path.join(os.path.dirname(PACKAGE), "tools", "data")


def home(*parts: str) -> str:
    return os.path.join(HOME, *parts)


def bundled(name: str) -> str:
    return os.path.join(BUNDLED, name)


def table(name: str) -> str:
    return os.path.join(TABLES, name)
