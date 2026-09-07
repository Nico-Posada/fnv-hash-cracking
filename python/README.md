# fnvcrack Python Package

Python bindings for the FNV-1a cracking library.

## Requirements

- Python 3.11, 3.12, 3.13, or 3.14
- uv
- GMP
- FLINT
- C compiler

On Ubuntu and Debian:

```bash
sudo apt install build-essential libgmp-dev libflint-dev -y
```

On macOS with Homebrew:

```bash
brew install flint pkgconf
```

The extension build discovers FLINT, GMP, and MPFR through `pkg-config`. For a nonstandard FLINT
installation, add the directory containing `flint.pc` to `PKG_CONFIG_PATH` instead of passing include
and library paths to the compiler.

## Development With uv

From the repository root:

```bash
uv sync --group dev
uv run pytest tests
```

To verify all supported Python versions:

```bash
uv python install 3.11 3.12 3.13 3.14

for py in 3.11 3.12 3.13 3.14; do
    uv venv --python "$py" ".venv-$py"
    UV_PROJECT_ENVIRONMENT=".venv-$py" uv sync --group dev
    UV_PROJECT_ENVIRONMENT=".venv-$py" uv run python -c "import fnvcrack; print(fnvcrack.__version__)"
    UV_PROJECT_ENVIRONMENT=".venv-$py" uv run pytest tests
done
```

## Usage

```python
from fnvcrack import CrackContext

ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
result = ctx.crack(target_hash, crack_len=8)

if result.ok:
    print(result.value)
```

`crack_len` is the number of unknown bytes to crack. Known prefixes and suffixes
do not count toward it.

If you do not know the length, enable incremental search:

```python
result = ctx.crack(target_hash, crack_len=12, incremental=True)
```

Set `threads` above `1` to enumerate one shared lattice with native workers:

```python
result = ctx.crack(target_hash, crack_len=12, threads=4)
```

The default is `threads=1`. The value is a maximum worker count for one shared
lattice enumeration per exact search length. Short searches can be slower due
to worker and scheduling overhead.

Tune enumeration when you want a wider local search:

```python
result = ctx.crack(
    target_hash,
    crack_len=12,
    enum_bound=4,
)
```

`enum_bound` controls the local lattice search radius around the solver guess.
Lower values are faster and can miss harder cases. Higher values improve coverage
but can be much slower.

To inspect multiple verified matches, pass a callback. A truthy return accepts
the current candidate and stops; a falsey return rejects it and continues:

```python
matches = []

def accept(candidate):
    matches.append(candidate)
    return len(matches) == 10

result = ctx.crack(target_hash, crack_len=8, callback=accept)
```

The callback receives each complete verified candidate as `bytes`, including
the configured prefix and suffix. With no callback, `crack()` keeps its
first-match behavior. Returning false every time runs until bounded exhaustion
and returns `CrackResult(FAILED, None)` even though `matches` contains every
offered result.

With multiple threads, callback calls are fully serialized even if a callback
releases the GIL. Candidate order is unspecified.

## API

```python
CrackContext(
    offset_basis=0xcbf29ce484222325,
    prime=0x100000001b3,
    bit_length=64,
    valid_chars=None,
    prefix=b"",
    suffix=b"",
)
```

`max_enum_candidates=0` means unlimited.

`ctx.crack(...)` returns a `CrackResult`:

```python
CrackResult(status=0, value=b"...")
```

`result.ok` is true when `status == 0`.
