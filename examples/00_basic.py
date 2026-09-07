from common import *
from fnvcrack import CrackContext

hashed = fnv1a_default(b"secret")

# "unknown" hashed value where we know the value is printable chars
ctx = CrackContext(valid_chars=string.printable.encode())

print(f"{hashed = :#x}")
with timer():
    # we "dont know" what the hashed string length is, so put a maximum search length
    # to 10 and search all lengths up to it (inclusive)
    result = ctx.crack(hashed, 10, incremental=True)

print(f"{result.ok = }")
if result.ok:
    print(f"{result.value = }")

r"""
hashed = 0xab23f0eec020c951
Took 0.000307s
result.ok = True
result.value = b'secret'
"""
