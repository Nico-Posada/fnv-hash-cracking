from common import *
import fnvcrack

# Another cool feature is the ability to generate funny
# collisions on constrained inputs. An example is for
# Call of Duty dvars - the function to hash them is given below

EXTRA = 'q6n-+7=tyytg94_*'
def hash_dvar(dvar : str|bytes):
    OFFSET_BASIS = 0xD86A3B09566EBAAC
    PRIME = 0x10000000233
    MOD = 2 ** 64
    _hash = OFFSET_BASIS
    dvar = dvar if isinstance(dvar, str) else dvar.decode()

    VALID_CHARSET = string.ascii_letters + string.digits + "_#"
    assert all(char in VALID_CHARSET for char in dvar)

    NEW_STR = dvar[0] + EXTRA + dvar[1:]

    for char in NEW_STR.lower():
        _hash = PRIME * (ord(char) ^ _hash) % MOD

    return _hash

# to "protect" the hashes they inject random garbage at the start after the
# first letter. This is more annoying if you're trying to recover the original
# string, but since we want to generate a collision it doesn't really matter

collision_prefix = b"super_epic_fnvcrack_demo#"
collision_prefix = (
    collision_prefix[:1] +
    EXTRA.encode() +
    collision_prefix[1:]
)

hashed = hash_dvar(b"normal_dvar")
print(f"{hashed = :#x}")

ctx = fnvcrack.CrackContext(
    # odd values defined in the hash function
    prime=0x10000000233,
    offset_basis=0xD86A3B09566EBAAC,
    prefix=collision_prefix,
    # the hash function converts our string to lowercase so we can't have any upper chars
    valid_chars=(string.ascii_lowercase + string.digits + "_#").encode()
)

with timer():
    # using a search length of 13 is typically a bit absurd, but since our charset is
    # so constrained, we actually need a search space this large.
    result = ctx.crack(hashed, 13)

print(f"{result.ok = }")
if result.ok:
    collision = result.value[:1] + result.value[len(EXTRA)+1:]
    print(f"{collision = !r}")
    print(f"{hashed == hash_dvar(collision) = }")

# it takes a while but a solution is eventually found
r"""
hashed = 0xfc16f5bc05480e73
Took 139.906241s
result.ok = True
collision = b'super_epic_fnvcrack_demo#y4n7yn7hfxqix'
hashed == hash_dvar(collision) = True
"""
