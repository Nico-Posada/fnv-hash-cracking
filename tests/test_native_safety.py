import resource
import unittest
from contextlib import contextmanager

from fnvcrack import CrackContext


def _current_address_space():
    with open("/proc/self/statm") as f:
        pages = int(f.read().split()[0])
    return pages * resource.getpagesize()


@contextmanager
def _limited_address_space(extra_bytes):
    old_limit = resource.getrlimit(resource.RLIMIT_AS)
    soft = _current_address_space() + extra_bytes
    hard = old_limit[1]
    if hard != resource.RLIM_INFINITY:
        soft = min(soft, hard)

    resource.setrlimit(resource.RLIMIT_AS, (soft, hard))
    try:
        yield
    finally:
        resource.setrlimit(resource.RLIMIT_AS, old_limit)


class NativeSafetyTestCase(unittest.TestCase):
    def test_repeated_construction_and_destruction_does_not_crash(self):
        for _ in range(1000):
            CrackContext(
                prefix=b"pre",
                suffix=b"suf",
                valid_chars=b"abcdefghijklmnopqrstuvwxyz",
            )

    def test_failed_constructors_do_not_crash_or_poison_future_contexts(self):
        for _ in range(100):
            with self.assertRaises(TypeError):
                CrackContext(prefix="bad")

            with self.assertRaises(TypeError):
                CrackContext(bit_length=8, prime=256)

        ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
        self.assertEqual(ctx.bit_length, 64)

    def test_failed_bit_length_constructors_do_not_leak(self):
        with _limited_address_space(32 * 1024 * 1024):
            for _ in range(500000):
                with self.assertRaises(ValueError):
                    CrackContext(bit_length=0)

    def test_large_fmpz_conversion_does_not_crash(self):
        value = (1 << 4096) + 123
        ctx = CrackContext(offset_basis=value, prime=value - 2, bit_length=4097)
        self.assertEqual(ctx.offset_basis, value)
        self.assertEqual(ctx.prime, value - 2)

    def test_large_bit_length_target_validation_does_not_allocate_modulus(self):
        ctx = CrackContext(offset_basis=0, prime=1, bit_length=500000000)
        with _limited_address_space(16 * 1024 * 1024):
            result = ctx.crack(0, crack_len=0)
        self.assertEqual(result.status_name, "BAD_SEARCH_LENGTH")
