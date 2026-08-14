from common import *
import fnvcrack
import random

# assume a scenario where you have a list of hashed names
# (maybe a bunch of hashed asset names?) You can use batch_crack to
# parallelize the cracking process for them

if __name__ == "__main__": # we need the guard since this uses multiprocessing under the hood
    CHARSET = (string.ascii_letters + string.digits + "_.").encode()
    hashed_names = [ # 1000 random names of length 6-10
        fnv1a_default(bytes(CHARSET[random.randint(0, len(CHARSET)-1)] for _ in range(random.randint(6, 10))))
        for _ in range(1000)
    ]

    ctx = fnvcrack.CrackContext(
        valid_chars=CHARSET
    )

    with timer():
        results = ctx.batch_crack(hashed_names, 10, incremental=True)
    
    total = sum(1 for r in results if r is not None)
    print(f"Cracked {total}/{len(results)}")
    
    # and here's how to visualize them (only first 10)
    joined = list(zip(hashed_names, results))
    for hashed, cracked in joined[:10]:
        print(f"{hashed:#x} => {cracked!r}")

r"""
Took 1.983369s
Cracked 1000/1000
0xd097a2f675101a18 => b'2VXzU2'
0x677cfb32af63f39 => b'ER25p2'
0xb580031b9bd19bc2 => b'95ZFzA'
0xd4b26352074e607c => b'26SC00an'
0xaddf2e7dd4153fca => b'zyd4wW'
0xbbf5fa447fa9077 => b'KPQxM979dN'
0x45646013e34320d => b'uo3rnXQ'
0x87d516778336c18a => b'khXA3Nf'
0x36b75aed4483f25c => b'V7yHWL65.E'
0x8e632a75b36333af => b'1oVvvvR.Fd'
"""
