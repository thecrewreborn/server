# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
import os

_FLAGS = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)


def write_atomic(path: str, data: bytes, private: bool = False,
                 owner: tuple[int, int] | None = None) -> os.stat_result:
    tmp = path + ".tmp"
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    try:
        os.unlink(tmp)
    except FileNotFoundError:
        pass
    fd = os.open(tmp, _FLAGS, 0o600 if private else 0o666)
    try:
        with os.fdopen(fd, "wb") as f:
            if private and hasattr(os, "fchmod"):
                os.fchmod(f.fileno(), 0o600)
            if owner is not None:
                os.fchown(f.fileno(), *owner)
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
            st = os.fstat(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    return st
