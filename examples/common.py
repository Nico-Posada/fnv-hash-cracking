from functools import partial
from contextlib import contextmanager
import time
import string

from fnvcrack import context

def fnv1a(val: bytes, prime: int, offset_basis: int, bits: int):
    h = offset_basis
    for c in val:
        h ^= c
        h *= prime
        h &= (1 << bits) - 1 

    return h

@contextmanager
def timer():
    start = time.time()
    yield
    end = time.time()

    print(f"Took {end - start:.6f}s")


fnv1a_64 = partial(fnv1a, bits=64)
fnv1a_default = partial(fnv1a_64, prime=0x100000001b3, offset_basis=0xcbf29ce484222325)
