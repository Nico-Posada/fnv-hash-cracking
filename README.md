# fnv-hash-cracking

Crack hashes or find collisions for hashes hashed with the [FNV-1a](https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash) algorithm without exhaustive search.

This implementation uses bounded lattice enumeration with FLINT-backed reduction to efficiently crack FNV-1a hashes, supporting both standard 64-bit hashes and arbitrary-precision variants.

## Credits

Huge thank you to [ConnorM](https://connor-mccartney.github.io) for his [incredible writeup](https://connor-mccartney.github.io/cryptography/other/Trying-to-crack-COD-FNV-hashes) and writing the original Python proof of concept on which this is based. He does some incredible work with cryptography and his writeups are worth a read.

## Requirements

This project requires the following libraries:
- [GMP](https://gmplib.org) - GNU Multiple Precision Arithmetic Library
- [FLINT](https://flintlib.org) - Fast Library for Number Theory

### Installation

#### Ubuntu and Debian
```bash
sudo apt install build-essential libgmp-dev libflint-dev -y
```

For other platforms or building from source, consult the [GMP documentation](https://gmplib.org/manual/) and [FLINT documentation](https://flintlib.org/doc/).

## Building

Clone the repository and build the project:

```bash
git clone https://github.com/Nico-Posada/fnv-hash-cracking.git
cd fnv-hash-cracking
make
```

This will produce a `main` executable which runs a few examples of cracking different types of FNV-1a hashes.

## Usage

The library provides a flexible API for cracking FNV-1a hashes with known or unknown string lengths:

### Basic Example (64-bit)

```c
// example from src/main.c

#include "context.h"
#include "crack.h"

CREATE_CONTEXT(ctx);
init_crack_ctx(
    ctx,
    0xCBF29CE484222325,  // FNV-1a 64-bit offset basis
    0x100000001B3,        // FNV-1a 64-bit prime
    64,                   // bit size
    "abcdefghijklmnopqrstuvwxyz",  // valid characters in solution
    "prefix_",            // known prefix (or NULL)
    "_suffix"             // known suffix (or NULL)
);

char_buffer result = {NULL, 0}; // buf, size (keep as NULL, 0) when initializing
uint64_t target_hash = 0x1234567890ABCDEF;

// Crack with known length
// 13 is the expected length of the string (including prefix/suffix)
CrackResult status = crack_u64_with_len(ctx, target_hash, &result, 13);

// Or search across lengths
// 10 is the max unknown string length to search for
CrackResult status = crack_u64(ctx, target_hash, &result, 10);

if (status == SUCCESS) {
    printf("Cracked: %s\n", result.data);
}

clear_char_buffer(&result);
destroy_crack_ctx(ctx);
```

### Arbitrary Precision Example

For non-standard FNV-1a implementations with custom parameters:

```c
// example from src/main.c

#include <flint/fmpz.h>
#include "context.h"
#include "crack.h"

// create fmpz numbers like normal
fmpz_t offset_basis, prime, target;
fmpz_init(offset_basis);
fmpz_init(prime);
fmpz_init(target);
fmpz_set_str(offset_basis, "86478568332086667988955226522744024433416290808708427009300709942571393030379", 10);
fmpz_set_str(prime, "58212954222403626346155684772977216669103315464820228336508867619615003388891", 10);
fmpz_set_str(hashed, "923278209713176653012807450506579337424686596606979155232335733448961331039798473007051981204278", 10);

CREATE_CONTEXT(ctx);
init_crack_fmpz_ctx(
    ctx,
    offset_basis,
    prime,
    320,  // bit size
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ ",
    NULL,  // no known prefix
    NULL   // no known suffix
);

char_buffer result = {NULL, 0};
// We know the string is 37 chars long.
CrackResult status = crack_fmpz_with_len(ctx, target, &result, 37);

if (status == SUCCESS) {
    printf("Cracked: %s\n", result.data);
}

// clear fmpz vars like normal
fmpz_clear(offset_basis);
fmpz_clear(prime);
fmpz_clear(target);

clear_char_buffer(&result);
destroy_crack_ctx(ctx);
```

## API Overview

### Context Management
- `init_crack_ctx()` - Initialize context for 64-bit FNV-1a
- `init_crack_fmpz_ctx()` - Initialize context for arbitrary precision FNV-1a
- `destroy_crack_ctx()` - Clean up context resources

### Cracking Functions
- `crack_u64_with_len()` - Crack 64-bit hash with known total length
- `crack_u64()` - Crack 64-bit hash searching across length range
- `crack_fmpz_with_len()` - Crack arbitrary precision hash with known length
- `crack_fmpz()` - Crack arbitrary precision hash searching across lengths

### Parameters
- `expected_len` - Total length of the hashed string (including prefix/suffix)
- `max_search_len` - Maximum string length to search when length is unknown

## How It Works

This tool builds a lattice for the FNV-1a hash inversion problem, reduces the basis with FLINT, then runs bounded local enumeration around the solver guess. Known prefixes and suffixes are folded into the target before solving, and `valid_chars` constrains candidate bytes.

## Python Extension

The [python extension](./python) for this is still WIP. It's usable at the moment for linux distros, but it's not very stable. I will work on improving this in the coming months.

## License

See LICENSE file for details.
