import os
import subprocess
import sys
import textwrap

from fnvcrack import FNV64_OFFSET_BASIS, FNV64_PRIME


LOWER = b"abcdefghijklmnopqrstuvwxyz"
UPPER = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
DIGITS = b"0123456789"
ALNUM = LOWER + UPPER + DIGITS
PRINTABLE = (
    DIGITS
    + LOWER
    + UPPER
    + b"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ "
)
FULL_BYTES = bytes(range(256))


def fnv(data, offset_basis=FNV64_OFFSET_BASIS, prime=FNV64_PRIME, bits=64):
    mask = (1 << bits) - 1
    hsh = offset_basis
    for c in data:
        hsh ^= c
        hsh *= prime
        hsh &= mask
    return hsh


def run_python(code, timeout=10):
    env = os.environ.copy()
    proc = subprocess.Popen(
        [sys.executable, "-c", textwrap.dedent(code)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        proc.kill()
        stdout, stderr = proc.communicate()
        raise AssertionError(
            f"subprocess timed out\nstdout={stdout}\nstderr={stderr}"
        ) from exc

    return proc.returncode, stdout, stderr


class CrackAssertionsMixin:
    def assertCracked(self, result, target, ctx):
        self.assertTrue(result.ok)
        self.assertIsNotNone(result.value)
        self.assertEqual(
            fnv(result.value, ctx.offset_basis, ctx.prime, ctx.bit_length),
            target,
        )
        return result.value
