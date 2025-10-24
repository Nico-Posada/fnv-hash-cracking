# fnvcrack - Python Extension

High-performance Python bindings for the FNV-1a hash cracking library using lattice-based cryptanalysis.

##  Disclaimer

**This Python extension is currently a Work In Progress (WIP) and may not work on all machines or platforms.** It has primarily been tested on Linux distributions. Use at your own risk and expect potential stability issues.

## Installation

### Building from Source

```bash
cd python
python3 setup.py build
```

### Installing

```bash
python3 setup.py install
```

For development/testing, you can also use:
```bash
python3 setup.py develop
```

## Requirements

- Python 3.8+
- GMP library
- FLINT library
- C compiler with LTO support (gcc/clang are fine)

## Usage

### Basic Example - 64-bit FNV-1a

```python
from fnvcrack import CrackContext
import string

# Create a context with standard 64-bit FNV-1a parameters
charset = (string.ascii_letters + string.digits).encode()
ctx = CrackContext(
    offset_basis=0xcbf29ce484222325,
    prime=0x00000100000001b3,
    bit_length=64,
    brute_chars=charset,
    valid_chars=charset
)

# Hash to crack
target_hash = 0x1234567890abcdef

# Attempt to crack the hash
# Parameters: (hash, max_search_length, max_crack_length)
# If it fails to find a plaintext of length <= 8, it will begin
# to incorporate brute force into the search up to and including length 11
error_code, result = ctx.crack(target_hash, 11, 8)

if error_code >= 0 and result:
    print(f"Cracked: {result}")
else:
    print("Failed to crack hash")
```

### Example with Prefix/Suffix

```python
from fnvcrack import CrackContext
import string

# If you know part of the plaintext (e.g., known prefix/suffix)
prefix = b"prefix_"
suffix = b"_suffix"

charset = (string.ascii_letters + string.digits + "_").encode()

ctx = CrackContext(
    offset_basis=0xcbf29ce484222325,
    prime=0x00000100000001b3,
    bit_length=64,
    prefix=prefix, # set prefix
    suffix=suffix, # set suffix
    valid_chars=charset,
    brute_chars=charset
)

target_hash = 0x1234567890abcdef

# max search length does not include prefix/suffix
error_code, result = ctx.crack(target_hash, 11, 8)

if error_code >= 0 and result:
    print(f"Cracked: {result}")
else:
    print("Failed to crack hash")
```

### Example with Larger Bit Lengths (128-bit, 256-bit, etc.)

```python
from fnvcrack import CrackContext
import string

charset = (string.ascii_letters + string.digits + "_").encode()

ctx = CrackContext(
    offset_basis=0x6c62272e07bb014262b821756295c58d,
    prime=0x0000000001000000000000000000013b,
    bit_length=128,
    brute_chars=charset,
    valid_chars=charset
)

target_hash = 0x8d552ee4c3066d0eea5ed959c0c80f67

# max search length does not include prefix/suffix
error_code, result = ctx.crack(target_hash, 16, 16)

if error_code >= 0 and result:
    print(f"Cracked: {result}")
else:
    print("Failed to crack hash")
```

### Understanding the `crack()` Method

```python
error_code, result = ctx.crack(hash_value, max_search_length, max_crack_length)
```

**Parameters:**
- `hash_value` (int): The FNV-1a hash to crack
- `max_search_length` (int): Maximum total string length to search for
- `max_crack_length` (int): Maximum characters in the unknown portion to attempt

**Returns:**
- `error_code` (int): Status code (0 = success, -1 = failed, other negatives = errors)
- `result` (bytes or None): The cracked plaintext if successful

**Error Codes:**
- `0` = SUCCESS
- `-1` = FAILED (couldn't find plaintext)
- `-2` = MEMORY_ERROR
- `-3` = MISSING_BRUTE_CHARS
- `-4` = CONTEXT_UNINITIALIZED
- `-5` = BAD_SEARCH_LENGTH

## Context Properties

The `CrackContext` object exposes several read-only properties:

```python
ctx = CrackContext(
    offset_basis=0xcbf29ce484222325,
    prime=0x00000100000001b3,
    bit_length=64,
    prefix=b'test',
    suffix=b'end',
    valid_chars=b'abc',
    brute_chars=b'xyz'
)

print(ctx.offset_basis)   # 0xcbf29ce484222325
print(ctx.prime)          # 0x00000100000001b3
print(ctx.bit_length)     # 64
print(ctx.prefix)         # b'test'
print(ctx.suffix)         # b'end'
print(ctx.valid_chars)    # b'abc'
print(ctx.brute_chars)    # b'xyz'
```

## Default Values

If parameters are omitted, the following defaults are used:

- `offset_basis`: `0xcbf29ce484222325` (FNV-1a 64-bit offset basis)
- `prime`: `0x00000100000001b3` (FNV-1a 64-bit prime)
- `bit_length`: `64`
- `prefix`: `b''` (empty)
- `suffix`: `b''` (empty)
- `valid_chars`: All 256 possible byte values
- `brute_chars`: `b''` (empty - no brute forcing)

## Running Tests

```bash
cd tests
python3 test.py
```

## Notes

- All string parameters (`prefix`, `suffix`, `valid_chars`, `brute_chars`) must be **bytes objects**, not strings
- The extension supports arbitrary precision integers for `offset_basis`, `prime`, and hash values
- Larger bit lengths (128, 256, 512) are supported but may be slower
- The lattice-based approach works best when you have constraints on the character set or known prefix/suffix
