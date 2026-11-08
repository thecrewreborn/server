# The Crew Reborn - Knutwurst
# https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
from cryptography.hazmat.decrepit.ciphers.algorithms import Blowfish
from cryptography.hazmat.primitives.ciphers import Cipher, modes


def _swap(data: bytes) -> bytes:
    return b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4))


def _pad8(data: bytes) -> bytes:
    return data + b"\0" * (-len(data) % 8)


def encrypt(key: bytes, data: bytes) -> bytes:
    enc = Cipher(Blowfish(key), modes.ECB()).encryptor()
    return _swap(enc.update(_swap(_pad8(data))) + enc.finalize())


def decrypt(key: bytes, data: bytes) -> bytes:
    data = data[:len(data) - len(data) % 8]
    dec = Cipher(Blowfish(key), modes.ECB()).decryptor()
    return _swap(dec.update(_swap(data)) + dec.finalize())
