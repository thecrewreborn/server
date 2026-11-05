# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
def decompress(src: bytes) -> bytes:
    out = bytearray()
    ip = 0

    def run(t: int, base: int) -> int:
        nonlocal ip
        if t:
            return t
        t = 0
        while src[ip] == 0:
            t += 255
            ip += 1
        t += base + src[ip]
        ip += 1
        return t

    def copy(pos: int, n: int) -> None:
        for _ in range(n):
            out.append(out[pos])
            pos += 1

    state = "loop"
    t = src[0]
    if t > 17:
        ip = 1
        t -= 17
        out += src[ip:ip + t]
        ip += t
        if t < 4:
            t = src[ip]
            ip += 1
            state = "match"
        else:
            state = "first"
    while True:
        if state == "loop":
            t = src[ip]
            ip += 1
            if t >= 16:
                state = "match"
                continue
            t = run(t, 15) + 3
            out += src[ip:ip + t]
            ip += t
            state = "first"
        if state == "first":
            t = src[ip]
            ip += 1
            if t >= 16:
                state = "match"
                continue
            pos = len(out) - 0x801 - (t >> 2) - (src[ip] << 2)
            ip += 1
            copy(pos, 3)
            t = src[ip - 2] & 3
        elif state == "match":
            if t >= 64:
                pos = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3)
                ip += 1
                copy(pos, (t >> 5) + 1)
            elif t >= 32:
                t = run(t & 31, 31)
                pos = len(out) - 1 - (src[ip] >> 2) - (src[ip + 1] << 6)
                ip += 2
                copy(pos, t + 2)
            elif t >= 16:
                pos = len(out) - ((t & 8) << 11)
                t = run(t & 7, 7)
                pos -= (src[ip] >> 2) + (src[ip + 1] << 6)
                ip += 2
                if pos == len(out):
                    return bytes(out)
                copy(pos - 0x4000, t + 2)
            else:
                pos = len(out) - 1 - (t >> 2) - (src[ip] << 2)
                ip += 1
                copy(pos, 2)
            t = src[ip - 2] & 3
        if t == 0:
            state = "loop"
            continue
        out += src[ip:ip + t]
        ip += t
        t = src[ip]
        ip += 1
        state = "match"
