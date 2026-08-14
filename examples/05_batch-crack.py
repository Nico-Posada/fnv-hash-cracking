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
        results = ctx.batch_crack(
            hashed_names,
            10,
            incremental=True,
            processes=4,  # None uses multiprocessing's default worker count
        )
    
    total = sum(1 for r in results if r is not None)
    print(f"Cracked {total}/{len(results)}")
    
    # and here's how to visualize them (only first 10)
    joined = list(zip(hashed_names, results))
    for hashed, cracked in joined[:10]:
        print(f"{hashed:#x} => {cracked!r}")

r"""
Took 3.443349s
Cracked 1000/1000
0x599e78e1886f1ea8 => b'XZ_zXTkhut'
0x5bdd20bf2a25a9e7 => b'oZoNtZVz'
0x7f5a1f94c9086fab => b'ufFSnP'
0xaf6bc6d7cf9d9c91 => b'_9._05'
0x78e95fa38b6c8965 => b'7sWUZP'
0x335360b10368c014 => b'5xr1maugC'
0x91249347a486a91a => b'76qDno'
0xe58dbc5f4d13671a => b't4EtLS1ZH'
0x4352cf264b5a7d0d => b'kx6BnVDu'
0x5c1b826b8217cd91 => b'qFPkLIW'
"""
