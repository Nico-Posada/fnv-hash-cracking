# fnv-hash-cracking

[![Wheels](https://github.com/Nico-Posada/fnv-hash-cracking/actions/workflows/wheels.yml/badge.svg)](https://github.com/Nico-Posada/fnv-hash-cracking/actions/workflows/wheels.yml)

Crack hashes or find collisions for data hashed with the
[FNV-1a](https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash)
algorithm without exhaustive search.

This implementation uses bounded lattice enumeration with FLINT-backed
reduction to efficiently crack FNV-1a hashes. It supports standard 64-bit
hashes, custom FNV parameters, and arbitrary-precision variants.

FNV-1a is not a cryptographic hash. This project is intended for reverse
engineering, data recovery, collision research, and similar work involving
known FNV-1a parameters.

## Credits

Huge thank you to [Connor McCartney](https://connor-mccartney.github.io) for
his [writeup](https://connor-mccartney.github.io/cryptography/other/Trying-to-crack-COD-FNV-hashes)
and the original Python proof of concept on which this project is based.

## Requirements

The Python package supports Python 3.11, 3.12, 3.13, and 3.14.

Prebuilt wheels include the native libraries required by `fnvcrack` and are
available for:

- Linux x86_64
- Windows AMD64
- macOS x86_64
- macOS arm64

## Installation

Install the package from PyPI with pip:

```bash
pip install fnvcrack
```

Or add it to a uv project:

```bash
uv add fnvcrack
```

### Examples

The Python package provides a high-level API for standard and custom FNV-1a
hashes.

More runnable demos are available in the [`examples/`](examples/) directory.

#### Basic Example

```python
from fnvcrack import CrackContext

target_hash = 0x25DA8C1836A8D66D
ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
result = ctx.crack(target_hash, crack_len=8)

if result.ok:
    print(result.value)
```

This prints:

```text
b'abcdefgh'
```

`crack_len` is the number of unknown bytes. Known prefixes and suffixes do not
count toward it.

#### Known Prefixes and Suffixes

```python
ctx = CrackContext(
    prefix=b"archive/",
    suffix=b".bin",
    valid_chars=b"0123456789abcdef",
)
result = ctx.crack(target_hash, crack_len=8)
```

#### Unknown Length

Set `incremental=True` to search every unknown length from 1 through
`crack_len`:

```python
result = ctx.crack(target_hash, crack_len=12, incremental=True)
```

#### Enumeration Limits

Use `enum_bound` to control the local search radius around the lattice
solution. Larger values search more candidates but can be much slower.

```python
result = ctx.crack(
    target_hash,
    crack_len=8,
    enum_bound=6,
    max_enum_candidates=1_000_000,
)
```

`max_enum_candidates=0` means unlimited. Setting `valid_chars=None` allows all
256 byte values, including NUL bytes.

#### Inspecting Multiple Matches

By default, `crack()` stops at the first verified match. Pass a callback to
inspect each complete verified candidate as `bytes`. A truthy return accepts
that candidate and stops; a falsey return rejects it and continues:

```python
matches = []

def accept(candidate):
    matches.append(candidate)
    return len(matches) == 10

result = ctx.crack(target_hash, crack_len=8, callback=accept)
```

Returning false every time runs until the bounded search is exhausted. The
result is then `CrackResult(FAILED, None)` even though `matches` contains every
candidate offered to the callback.

#### Batch Cracking

`batch_crack()` cracks multiple targets in parallel and preserves their input
order:

```python
from fnvcrack import CrackContext
target_hashes = [
    0x25DA8C1836A8D66D,
    0x27E64FF62868579D,
]

if __name__ == "__main__":
    ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
    values = ctx.batch_crack(target_hashes, crack_len=8)
```

The `if __name__ == "__main__"` guard is required on platforms that use
spawn-based multiprocessing.

## Python API Overview

### Context

```python
CrackContext(
    offset_basis=0xCBF29CE484222325,
    prime=0x100000001B3,
    bit_length=64,
    valid_chars=None,
    prefix=b"",
    suffix=b"",
)
```

Python integers are accepted for custom offset bases, primes, targets, and
arbitrary-precision hashes.

### Results

`CrackContext.crack()` returns a `CrackResult` with:

- `status`: A `CrackStatus` value
- `value`: The matching bytes, or `None`
- `ok`: `True` when the crack succeeded
- `status_name`: The status as a string

## C Usage

The repository also exposes the underlying C API:

```c
#include <inttypes.h>
#include <stdio.h>

#include "context.h"
#include "crack.h"

int main(void) {
    CREATE_CONTEXT(ctx);
    if (!init_crack_ctx(
            ctx,
            UINT64_C(0xCBF29CE484222325),
            UINT64_C(0x100000001B3),
            64,
            "abcdefghijklmnopqrstuvwxyz",
            NULL,
            NULL
        )) {
        return 1;
    }

    char_buffer result = {NULL, 0};
    CrackResult status = crack_u64_with_len(
        ctx,
        UINT64_C(0x25DA8C1836A8D66D),
        &result,
        8
    );

    if (status == SUCCESS) {
        printf("Cracked: %s\n", result.data);
    }

    clear_char_buffer(&result);
    destroy_crack_ctx(ctx);
    return status == SUCCESS ? 0 : 1;
}
```

### C API Overview

#### Context Management

- `init_crack_ctx()` initializes a context for 64-bit FNV-1a.
- `init_crack_fmpz_ctx()` initializes an arbitrary-precision context.
- The corresponding `_with_len()` initializers accept binary-safe buffers.
- `destroy_crack_ctx()` releases context resources.

#### Cracking Functions

- `crack_u64_with_len()` cracks a 64-bit hash with a known total length.
- `crack_u64()` searches 64-bit hashes across a range of total lengths.
- `crack_fmpz_with_len()` cracks an arbitrary-precision hash with a known
  total length.
- `crack_fmpz()` searches arbitrary-precision hashes across a range of total
  lengths.
- The corresponding `_limits()` functions accept custom enumeration limits.
- `crack_u64_with_len_callback_limits()` and
  `crack_fmpz_with_len_callback_limits()` add callbacks to fixed-length
  searches.
- `crack_u64_callback_limits()` and `crack_fmpz_callback_limits()` add
  callbacks to incremental-length searches.

The callback receives a complete verified `char_buffer`, including any prefix
and suffix. Its data is borrowed and valid only during the callback. Returning
true accepts the candidate and transfers its allocation to `out_buffer`;
returning false rejects it, frees it, and continues enumeration.


Unlike Python's `crack_len`, C `expected_len` and `max_search_len` include the
known prefix and suffix.

## How It Works

This tool folds known prefixes and suffixes into the FNV-1a state, builds a
lattice for the unknown bytes, reduces the basis with FLINT, and runs bounded
enumeration around the solver guess. Candidate bytes are constrained by
`valid_chars`, and every returned result is verified against the target hash.

This is a constrained search rather than a guarantee that every preimage will
be found. Wider character sets, longer unknown inputs, and larger enumeration
bounds increase the work substantially.

## Development

Run the test suite:

```bash
uv run --locked --group dev pytest
```

Build the Python extension in place:

```bash
make build-pyext
```

Run Python and native C coverage:

```bash
make coverage-c
```

Run the benchmark CLI:

```bash
uv run --locked --group benchmark python benchmarking/bench.py perf \
    -p '[a-z]{8}' -c '[a-z]' -n 100 -b 4 -m 0 -I
```

The Wheels workflow builds and tests CPython 3.11 through 3.14 on Linux,
Windows, and macOS. The Linux and Windows wheel builds consume committed
native dependency locks. Regenerate both only when the FLINT specification
changes:

```bash
make lock-native
```

## Building From Source

Building from source requires a C11 compiler, `pkg-config`, and
[FLINT](https://flintlib.org). FLINT depends on GMP and MPFR, which are
installed automatically by the package managers below.

### Ubuntu and Debian

```bash
sudo apt install -y build-essential pkg-config libflint-dev
```

### macOS with Homebrew

```bash
brew install flint pkgconf
```

The source build reads FLINT's `pkg-config` metadata, which supplies the
include and library directories for FLINT, GMP, and MPFR. For a nonstandard
FLINT installation, add the directory containing `flint.pc` to
`PKG_CONFIG_PATH`.

Clone the repository and install the development environment:

```bash
git clone https://github.com/Nico-Posada/fnv-hash-cracking.git
cd fnv-hash-cracking
uv sync --group dev
```

To build the standalone C example:

```bash
make
./main
```
