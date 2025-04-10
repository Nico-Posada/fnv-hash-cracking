# Python Interface
There's a lot of different ways to do this, but this should be a good base to start with.

To use this example, you need to have g++ and be able to compile with c++20. Clone this repository and run
```bash
cd examples/python-interface
python3 pyfnvcrack.py
```

This will run the example script and you should see it output the cracked hash. To use in your own program, just import-star pyfnvcrack and you can initialize the library as so:
```py
from pyfnvcrack import *

# NOTE: this takes in 3 optional positional args to specify paths.
# The defaults will only be valid if you stay in the default project location.
# Read the source to see which ones may need to be updated 
crack = CrackLib()
```
Then to build the library with your desired constants, add
```py
# These are just the fnv1a defaults
mod = crack.load_crack_lib(presets.OFFSET_DEFAULT, presets.PRIME_1B3)
```
Once that's loaded, you need to initialize it with your charsets
```py
# I'm having my valid charset and bruting charset both be idents
mod.init(presets.ident, presets.ident)
```
Finally, you can start cracking hashes
```py
HASH = 0x0123456789abcdef
MAX_LENGTH = 10
mod.crack_hash(HASH, MAX_LENGTH)
```

Remember that the larger your max length, the longer each it will take. Every time you increase it,
the odds of finding a valid collision start to increase exponentially. 10 is a good middle ground for speed
and reliability.