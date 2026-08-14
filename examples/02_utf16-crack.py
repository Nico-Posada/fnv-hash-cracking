from common import *
import fnvcrack

# a place I see fnv hashing done is when import obfuscators want to load
# a dll, they hash the utf16 bytes (the encoding windows uses for paths)
# to match the dll they want. it's probably easier to find the dll name in the
# process yourself but this is a fun way to figure out the hash

hashed = fnv1a_default(encoded := "NTDLL.dll".encode("utf-16le"))

# 18 chars is normally too much for this as it'll find a trillion collisions
# before the intended input, but since we know that every other byte is a NUL byte (we're assuming ascii filenames),
# we can interpret it as the prime multiplication happening twice (since num ^ 0 == num).
# with this in mind, we can set up the CrackContext like this and easily recover the original string
ctx = fnvcrack.CrackContext(
    prime=(fnvcrack.FNV64_PRIME ** 2) % 2**64,
    valid_chars=string.printable.encode()
)

print(f"{encoded = !r}")
print(f"{hashed = :#x}")
with timer():
    # for this example, lets assume we don't know the length of our expected result,
    # so lets set an upper bound of 11 chars (inclusive) and enable incremental mode
    # to search all lengths starting from 1
    result = ctx.crack(hashed, 11, incremental=True)

print(f"{result.ok = }")
if result.ok:
    print(f"{result.value = }")

r"""
encoded = b'N\x00T\x00D\x00L\x00L\x00.\x00d\x00l\x00l\x00'
hashed = 0x10588c20c5471cf9
Took 0.001836s
result.ok = True
result.value = b'NTDLL.dll'
"""
