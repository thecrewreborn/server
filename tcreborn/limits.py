# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import collections
import datetime
import threading
import time

LINES_PER_MIN = 20


class RateLimit:

    def __init__(self, count: int, window: float, keys: int = 65536):
        self.count, self.window, self.keys = count, window, keys
        self.times: collections.OrderedDict[object, collections.deque] = collections.OrderedDict()
        self.lock = threading.Lock()

    def allow(self, key, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        with self.lock:
            while self.times:
                first = next(iter(self.times.values()))
                if first and now - first[-1] < self.window and len(self.times) <= self.keys:
                    break
                self.times.popitem(last=False)
            times = self.times.get(key)
            if times is None:
                times = self.times[key] = collections.deque()
            self.times.move_to_end(key)
            while times and now - times[0] >= self.window:
                times.popleft()
            if len(times) >= self.count:
                return False
            times.append(now)
            return True


class LineLimit:

    def __init__(self, label: str, per_window: int = LINES_PER_MIN, window: float = 60.0):
        self.label, self.per_window, self.window = label, per_window, window

        self.said: collections.OrderedDict[object, float] = collections.OrderedDict()
        self.start, self.count, self.left_out = -1e9, 0, 0
        self.lock = threading.Lock()

    def allow(self, key, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        with self.lock:
            if now - self.start >= self.window:
                if self.left_out:
                    print(f"[{datetime.datetime.now():%H:%M:%S}] {self.label}: {self.left_out} line(s) from unverified "
                          f"senders suppressed", flush=True)
                self.start, self.count, self.left_out = now, 0, 0
            while self.said and now - next(iter(self.said.values())) >= self.window:
                self.said.popitem(last=False)
            if key in self.said or self.count >= self.per_window:
                self.left_out += 1
                return False
            self.said[key] = now
            self.count += 1
            return True
