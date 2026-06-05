import unittest

from fnvcrack import (
    CrackContext,
    DEFAULT_ENUM_BOUND,
    FNV64_OFFSET_BASIS,
    FNV64_PRIME,
)

from conftest import FULL_BYTES, LOWER


class ContextApiTestCase(unittest.TestCase):
    def test_imports_and_defaults(self):
        ctx = CrackContext()
        self.assertEqual(ctx.offset_basis, FNV64_OFFSET_BASIS)
        self.assertEqual(ctx.prime, FNV64_PRIME)
        self.assertEqual(ctx.bit_length, 64)
        self.assertEqual(ctx.prefix, b"")
        self.assertEqual(ctx.suffix, b"")
        self.assertEqual(ctx.brute_chars, b"")
        self.assertEqual(ctx.valid_chars, FULL_BYTES)
        self.assertEqual(DEFAULT_ENUM_BOUND, 4)

    def test_explicit_empty_valid_chars_matches_default_all_bytes(self):
        self.assertEqual(CrackContext(valid_chars=b"").valid_chars, FULL_BYTES)

    def test_none_context_args_use_defaults(self):
        ctx = CrackContext(
            offset_basis=None,
            prime=None,
            bit_length=None,
            prefix=None,
            suffix=None,
            valid_chars=None,
            brute_chars=None,
        )

        self.assertEqual(ctx.offset_basis, FNV64_OFFSET_BASIS)
        self.assertEqual(ctx.prime, FNV64_PRIME)
        self.assertEqual(ctx.bit_length, 64)
        self.assertEqual(ctx.prefix, b"")
        self.assertEqual(ctx.suffix, b"")
        self.assertEqual(ctx.valid_chars, FULL_BYTES)
        self.assertEqual(ctx.brute_chars, b"")

    def test_valid_chars_are_unique_and_ordered(self):
        ctx = CrackContext(valid_chars=b"baba\xff\x00")
        self.assertEqual(ctx.valid_chars, b"\x00ab\xff")

    def test_context_copies_buffer_inputs(self):
        prefix = bytearray(b"pre")
        suffix = bytearray(b"suf")
        valid_chars = bytearray(LOWER)
        brute_chars = bytearray(b"abc")

        ctx = CrackContext(
            prefix=prefix,
            suffix=memoryview(suffix),
            valid_chars=memoryview(valid_chars),
            brute_chars=brute_chars,
        )

        prefix[:] = b"xxx"
        suffix[:] = b"yyy"
        valid_chars[:] = b"z" * len(valid_chars)
        brute_chars[:] = b"zzz"

        self.assertEqual(ctx.prefix, b"pre")
        self.assertEqual(ctx.suffix, b"suf")
        self.assertEqual(ctx.valid_chars, LOWER)
        self.assertEqual(ctx.brute_chars, b"abc")

    def test_rejects_str_buffers(self):
        for name in ("prefix", "suffix", "valid_chars", "brute_chars"):
            with self.subTest(name=name):
                with self.assertRaises(TypeError):
                    CrackContext(**{name: "abc"})

    def test_rejects_non_buffer_objects(self):
        for name in ("prefix", "suffix", "valid_chars", "brute_chars"):
            with self.subTest(name=name):
                with self.assertRaises(TypeError):
                    CrackContext(**{name: 123})

    def test_rejects_non_contiguous_buffer_objects(self):
        for name in ("prefix", "suffix", "valid_chars", "brute_chars"):
            with self.subTest(name=name):
                with self.assertRaises(BufferError):
                    CrackContext(**{name: memoryview(bytearray(b"abcd"))[::2]})

    def test_rejects_bad_bit_lengths_and_types(self):
        with self.assertRaises(ValueError):
            CrackContext(bit_length=0)

        with self.assertRaises(ValueError):
            CrackContext(bit_length=-1)

        with self.assertRaises(TypeError):
            CrackContext(bit_length="64")

        with self.assertRaises(OverflowError):
            CrackContext(bit_length=2**32)

    def test_rejects_numbers_that_do_not_fit_bit_length(self):
        with self.assertRaises(TypeError):
            CrackContext(bit_length=8, offset_basis=256)

        with self.assertRaises(TypeError):
            CrackContext(bit_length=8, prime=256)

    def test_accepts_numbers_on_bit_length_boundary(self):
        ctx = CrackContext(bit_length=8, offset_basis=255, prime=255)
        self.assertEqual(ctx.offset_basis, 255)
        self.assertEqual(ctx.prime, 255)

    def test_rejects_negative_numbers(self):
        with self.assertRaises(ValueError):
            CrackContext(offset_basis=-1)

        with self.assertRaises(ValueError):
            CrackContext(prime=-1)

        with self.assertRaises(ValueError):
            CrackContext(offset_basis=-1, bit_length=128)

        with self.assertRaises(ValueError):
            CrackContext(prime=-1, bit_length=128)

    def test_large_fmpz_properties_round_trip(self):
        offset_basis = (1 << 1024) + 0x12345
        prime = (1 << 1023) + 0x1b3
        ctx = CrackContext(offset_basis=offset_basis, prime=prime, bit_length=1025)
        self.assertEqual(ctx.offset_basis, offset_basis)
        self.assertEqual(ctx.prime, prime)
        self.assertEqual(ctx.bit_length, 1025)

    def test_fmpz_equality_uses_large_parameters(self):
        offset_basis = (1 << 128) + 0x123
        prime = (1 << 127) + 0x1b3
        lhs = CrackContext(offset_basis=offset_basis, prime=prime, bit_length=129)
        same = CrackContext(offset_basis=offset_basis, prime=prime, bit_length=129)
        different = CrackContext(offset_basis=offset_basis, prime=prime + 2, bit_length=130)

        self.assertEqual(lhs, same)
        self.assertNotEqual(lhs, different)

    def test_equality_and_repr_include_configuration(self):
        lhs = CrackContext(valid_chars=LOWER, brute_chars=b"abc", prefix=b"pre", suffix=b"suf")
        same = CrackContext(valid_chars=LOWER, brute_chars=b"abc", prefix=b"pre", suffix=b"suf")
        different = CrackContext(valid_chars=LOWER, brute_chars=b"abc", prefix=b"pre")

        self.assertEqual(lhs, same)
        self.assertNotEqual(lhs, different)
        self.assertNotEqual(lhs, object())
        self.assertIn("CrackContext(", repr(lhs))
        self.assertIn("prefix=b'pre'", repr(lhs))
