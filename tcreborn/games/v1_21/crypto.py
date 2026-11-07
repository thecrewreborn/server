# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

from tcreborn.keys import IV0, K0


def _pad16(data: bytes) -> bytes:
    return data + b"\0" * (-len(data) % 16)


def encrypt(key: bytes, iv: bytes, data: bytes) -> bytes:
    enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return enc.update(_pad16(data)) + enc.finalize()


def decrypt(key: bytes, iv: bytes, data: bytes) -> bytes:
    dec = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    return dec.update(_pad16(data)) + dec.finalize()
