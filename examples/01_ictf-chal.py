from common import *
import fnvcrack

# To showcase the ability to crack hashes >64 bits, here's a
# solver for https://github.com/quasar098/ictf-archive/blob/master/round-56/FNV-1a-chal.py
# Cracking an FNV-1a hash that uses mod 2**320

PRIME = 58212954222403626346155684772977216669103315464820228336508867619615003388891
OFFSET_BASIS = 86478568332086667988955226522744024433416290808708427009300709942571393030379
TARGET_HASH = 923278209713176653012807450506579337424686596606979155232335733448961331039798473007051981204278

ctx = fnvcrack.CrackContext(
    offset_basis=OFFSET_BASIS,
    prime=PRIME,
    bit_length=320,
    # no need to overconstrain, all printable chars is fine for this
    valid_chars=string.printable.encode(),
    # flag is encoded in big endian, so we need to reverse the values
    prefix=b"}",
    suffix=b"{ftci",
)

with timer():
    result = ctx.crack(
        TARGET_HASH,
        # flag length is 37 but with known prefix/suffix we only need 31
        37 - 5 - 1,
    )

print(f"{result.ok = }")
if result.ok:
    print(f"{result.value = }")
    print(f"flag = {result.value[::-1]!r}")

r"""
Took 0.008705s
result.ok = True
result.value = b'}n0171dd4_07_l4uq3_750ml4_51_r0x{ftci'
flag = b'ictf{x0r_15_4lm057_3qu4l_70_4dd1710n}'
"""
