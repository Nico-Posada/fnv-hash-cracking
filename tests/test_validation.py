import unittest

from fnvcrack import CrackContext, CrackResult, CrackStatus


class ValidationTestCase(unittest.TestCase):
    def test_status_helpers(self):
        self.assertTrue(CrackResult(CrackStatus.SUCCESS, b"abc").ok)
        self.assertFalse(CrackResult(CrackStatus.FAILED, None).ok)
        self.assertEqual(
            CrackResult(CrackStatus.INTERRUPTED, None).status_name,
            "INTERRUPTED",
        )
        self.assertEqual(CrackResult(-999, None).status_name, "UNKNOWN")

    def test_crack_option_validation(self):
        ctx = CrackContext()
        with self.assertRaisesRegex(ValueError, "enum_bound"):
            ctx.crack(0, max_len=8, enum_bound=-1)

        with self.assertRaisesRegex(TypeError, "enum_bound"):
            ctx.crack(0, max_len=8, enum_bound="4")

        with self.assertRaisesRegex(ValueError, "max_enum_candidates"):
            ctx.crack(0, max_len=8, max_enum_candidates=-1)

        with self.assertRaisesRegex(OverflowError, "max_enum_candidates"):
            ctx.crack(
                0,
                max_len=8,
                max_enum_candidates=2**64,
            )

        with self.assertRaisesRegex(TypeError, "enum_bound"):
            ctx.crack(0, max_len=8, enum_bound=True)

    def test_crack_argument_validation(self):
        ctx = CrackContext()

        with self.assertRaisesRegex(TypeError, "target"):
            ctx.crack("0", max_len=8)

        with self.assertRaisesRegex(TypeError, "target"):
            ctx.crack(True, max_len=8)

        with self.assertRaisesRegex(TypeError, "max_len"):
            ctx.crack(0, max_len="8")

        with self.assertRaisesRegex(TypeError, "max_len"):
            ctx.crack(0, max_len=True)

        with self.assertRaisesRegex(ValueError, "max_len"):
            ctx.crack(0, max_len=-1)

        with self.assertRaisesRegex(OverflowError, "max_len"):
            ctx.crack(0, max_len=2**32)

        with self.assertRaisesRegex(OverflowError, "max_len"):
            CrackContext(prefix=b"x").crack(0, max_len=2**32 - 1)

        with self.assertRaisesRegex(ValueError, "target"):
            ctx.crack(-1, max_len=8)

        with self.assertRaisesRegex(OverflowError, "target"):
            ctx.crack(2**64, max_len=8)

        fmpz_ctx = CrackContext(bit_length=128)
        with self.assertRaisesRegex(ValueError, "target"):
            fmpz_ctx.crack(-1, max_len=8)

        with self.assertRaisesRegex(OverflowError, "target"):
            fmpz_ctx.crack(2**128, max_len=8)

        small_ctx = CrackContext(bit_length=8, offset_basis=0, prime=1)
        with self.assertRaisesRegex(OverflowError, "target"):
            small_ctx.crack(256, max_len=1)

    def test_native_method_requires_all_internal_arguments(self):
        ctx = CrackContext()
        with self.assertRaises(TypeError):
            ctx._native.crack(0, 8)

        with self.assertRaises(TypeError):
            ctx._native.crack(0, 8, 4, 0, 0)

    def test_native_method_validates_target_range(self):
        small_ctx = CrackContext(bit_length=8, offset_basis=0, prime=1)
        with self.assertRaisesRegex(OverflowError, "target"):
            small_ctx._native.crack(256, 1, 4, 0)

        fmpz_ctx = CrackContext(bit_length=128)
        with self.assertRaisesRegex(ValueError, "target"):
            fmpz_ctx._native.crack(-1, 1, 4, 0)

        with self.assertRaisesRegex(OverflowError, "target"):
            fmpz_ctx._native.crack(2**128, 1, 4, 0)

    def test_native_method_rejects_total_length_overflow(self):
        ctx = CrackContext(prefix=b"x")
        with self.assertRaisesRegex(OverflowError, "max_len"):
            ctx._native.crack(0, 2**32 - 1, 4, 0)
