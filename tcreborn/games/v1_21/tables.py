# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os

FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")


def path(name: str) -> str:
    return os.path.join(FOLDER, name)
