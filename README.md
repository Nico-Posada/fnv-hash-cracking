# fnv-hash-cracking

[![Wheels](https://github.com/Nico-Posada/fnv-hash-cracking/actions/workflows/wheels.yml/badge.svg)](https://github.com/Nico-Posada/fnv-hash-cracking/actions/workflows/wheels.yml) [![Python 3.11-3.14](https://img.shields.io/badge/python-3.11%20%7C%203.12%20%7C%203.13%20%7C%203.14-blue.svg)](https://www.python.org/downloads/)

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
and the original Python proof of concept on which this project is based. I'd also
like to thank Blupper for his work on [linineq](https://github.com/TheBlupper/linineq)
as a lot of the ideas from that are used here as well.

## Requirements

The Python package supports Python 3.11, 3.12, 3.13, 3.14, and the following:

- Linux x86_64
- Windows AMD64
- macOS x86_64
- macOS arm64

## Installation

Install the package using pip or your package manager of choice (as long as it can
pull from PyPI)

```bash
pip install fnvcrack
```

### Examples

The Python package provides an easy to use API for standard and custom FNV-1a
hashes.

More runnable demos are available in the [`examples/`](examples/) directory.

#### Basic Example

```python
from fnvcrack import CrackContext

target_hash = 0x25DA8C1836A8D66D
ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
# `crack_len` is the number of unknown bytes. Known prefixes
# and suffixes do not count toward it.
result = ctx.crack(target_hash, crack_len=8)

if result.ok:
    print(result.value)
```

This prints:

```text
b'abcdefgh'
```


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

```python
# Set `incremental=True` to search every unknown length from 1 through `crack_len`
result = ctx.crack(target_hash, crack_len=12, incremental=True)
```

#### Multithreaded Cracking

Set `threads` above `1` to enumerate one shared lattice with native workers:

```python
result = ctx.crack(target_hash, crack_len=8, threads=4)
```

The default is `threads=1`. The value is a maximum worker count for one shared
lattice enumeration per exact search length. Short searches can be slower due
to worker and scheduling overhead. Callback calls are fully serialized and
candidate order is unspecified.

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
