# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os
from dataclasses import dataclass, field


@dataclass
class Session:
    key: bytes = field(default_factory=lambda: os.urandom(32))
    iv: bytes = field(default_factory=lambda: os.urandom(16))
    connected: bool = False
    answered: set = field(default_factory=set)
    replies: dict = field(default_factory=dict)
    partial: dict = field(default_factory=dict)
    ctx: object = None
