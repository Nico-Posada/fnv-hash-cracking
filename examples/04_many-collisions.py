from common import *
import fnvcrack

# basic example to showcase the callback functionality
# to capture 10 inputs that hash to 0x6767676767676767

ctx = fnvcrack.CrackContext(
    valid_chars=bytes(range(0x20, 0x7f))
)

results = []
def callback(candidate: bytes):
    results.append(candidate)
    print(f"collision #{len(results)}: {candidate!r}")
    # the generator stops when we return `True`
    return len(results) >= 10

with timer():
    ctx.crack(0x6767676767676767, 11, incremental=True, callback=callback)

r"""
collision #1: b't$3$~[Ib*}'
collision #2: b'5Q.^tqJ~0y'
collision #3: b'4PCV*)Pz1Lw'
collision #4: b'm,v+<<32q`F'
collision #5: b'I22b3q*%C:7'
collision #6: b'gFPC~YtAcZS'
collision #7: b"7(h':F1=%r#"
collision #8: b" OV['?7B1gq"
collision #9: b'*$$W4`s)D5D'
collision #10: b"~r>l}'ZuGXt"
Took 0.320615s
"""
