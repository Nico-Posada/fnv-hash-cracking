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
from fnvcrack import CrackContext, CrackOptions, CrackStrategy

ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
result = ctx.crack(target_hash, max_len=8)

if result.ok:
    print(result.value)
```

Use ENUMERATE when you want a slower but more complete search:

```python
options = CrackOptions(
    strategy=CrackStrategy.ENUMERATE,
    enum_bound=4,
)

result = ctx.crack(target_hash, max_len=12, options=options)
```

`enum_bound` controls the local lattice search radius around the solver guess.
Lower values are faster and can miss harder cases. Higher values improve coverage
but can be much slower.

## API

```python
CrackContext(
    offset_basis=0xcbf29ce484222325,
    prime=0x100000001b3,
    bit_length=64,
    valid_chars=None,
    brute_chars=None,
    prefix=b"",
    suffix=b"",
)
```

```python
CrackOptions(
    strategy=CrackStrategy.LLL,
    enum_bound=4,
    max_enum_candidates=0,
    max_crack_len=None,
)
```

`max_enum_candidates=0` means unlimited.

`max_crack_len=None` defaults to `bit_length // 8`.

`ctx.crack(...)` returns a `CrackResult`:

```python
CrackResult(status=0, value=b"...")
```

`result.ok` is true when `status == 0`.
