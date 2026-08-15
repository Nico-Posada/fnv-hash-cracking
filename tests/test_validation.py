import unittest

from fnvcrack import CrackContext, CrackResult, CrackStatus
from fnvcrack._fnvcrack import NativeContext


class ValidationTestCase(unittest.TestCase):
    def test_status_helpers(self):
        result = CrackResult(CrackStatus.SUCCESS, b"abc")
        self.assertIsInstance(result, tuple)
        self.assertEqual(result, (CrackStatus.SUCCESS, b"abc"))
        self.assertTrue(result.ok)
        self.assertFalse(CrackResult(CrackStatus.FAILED, None).ok)
        self.assertEqual(
            CrackResult(CrackStatus.INTERRUPTED, None).status_name,
            "INTERRUPTED",
        )
        self.assertEqual(CrackResult(-999, None).status_name, "UNKNOWN")

    def test_crack_option_validation(self):
        ctx = CrackContext()
        with self.assertRaisesRegex(ValueError, "enum_bound"):
            ctx.crack(0, crack_len=8, enum_bound=-1)

        with self.assertRaisesRegex(TypeError, "^enum_bound must be an int, got 'str'$"):
            ctx.crack(0, crack_len=8, enum_bound="4")

        with self.assertRaisesRegex(ValueError, "max_enum_candidates"):
            ctx.crack(0, crack_len=8, max_enum_candidates=-1)

        with self.assertRaisesRegex(TypeError, "^max_enum_candidates must be an int, got 'str'$"):
            ctx.crack(0, crack_len=8, max_enum_candidates="4")

        with self.assertRaisesRegex(OverflowError, "max_enum_candidates"):
            ctx.crack(
                0,
                crack_len=8,
                max_enum_candidates=2**64,
            )

        with self.assertRaisesRegex(TypeError, "enum_bound"):
            ctx.crack(0, crack_len=8, enum_bound=True)

        with self.assertRaisesRegex(TypeError, "^incremental must be a bool, got 'str'$"):
            ctx.crack(0, crack_len=8, incremental="true")

        with self.assertRaisesRegex(TypeError, "incremental"):
            ctx.crack(0, crack_len=8, incremental=1)

        with self.assertRaisesRegex(TypeError, "incremental"):
            ctx.crack(0, crack_len=8, incremental=0)

        with self.assertRaisesRegex(TypeError, "^callback must be callable or None$"):
            ctx.crack(0, crack_len=8, callback=0)

    def test_callback_exception_propagates_unchanged(self):
        class SentinelError(Exception):
            pass

        sentinel = SentinelError("sentinel")
        ctx = CrackContext(offset_basis=0, prime=1, bit_length=8, prefix=b"\x00")

        def fail(_candidate):
            raise sentinel

        with self.assertRaises(SentinelError) as caught:
            ctx.crack(0, crack_len=0, callback=fail)
        self.assertIs(caught.exception, sentinel)

    def test_callback_uses_normal_truth_testing(self):
        ctx = CrackContext(offset_basis=0, prime=1, bit_length=8, prefix=b"\x00")
        result = ctx.crack(0, crack_len=0, callback=lambda _candidate: 1)

        self.assertEqual(result.status, CrackStatus.SUCCESS)
        self.assertEqual(result.value, b"\x00")

    def test_crack_argument_validation(self):
        ctx = CrackContext()

        with self.assertRaisesRegex(TypeError, "max_len"):
            ctx.crack(0, max_len=8)

        with self.assertRaisesRegex(TypeError, "target"):
            ctx.crack("0", crack_len=8)

        with self.assertRaisesRegex(TypeError, "target"):
            ctx.crack(True, crack_len=8)

        with self.assertRaisesRegex(TypeError, "crack_len"):
            ctx.crack(0, crack_len="8")

        with self.assertRaisesRegex(TypeError, "crack_len"):
            ctx.crack(0, crack_len=True)

        with self.assertRaisesRegex(ValueError, "crack_len"):
            ctx.crack(0, crack_len=-1)

        with self.assertRaisesRegex(OverflowError, "crack_len"):
            ctx.crack(0, crack_len=2**32)

        with self.assertRaisesRegex(OverflowError, "crack_len"):
            CrackContext(prefix=b"x").crack(0, crack_len=2**32 - 1)

        with self.assertRaisesRegex(ValueError, "target"):
            ctx.crack(-1, crack_len=8)

        with self.assertRaisesRegex(OverflowError, "target"):
            ctx.crack(2**64, crack_len=8)

        fmpz_ctx = CrackContext(bit_length=128)
        with self.assertRaisesRegex(ValueError, "target"):
            fmpz_ctx.crack(-1, crack_len=8)

        with self.assertRaisesRegex(OverflowError, "target"):
            fmpz_ctx.crack(2**128, crack_len=8)

        small_ctx = CrackContext(bit_length=8, offset_basis=0, prime=1)
        with self.assertRaisesRegex(OverflowError, "target"):
            small_ctx.crack(256, crack_len=1)

    def test_native_method_requires_all_internal_arguments(self):
        ctx = CrackContext()
        with self.assertRaises(TypeError):
            NativeContext.crack(ctx, 0, 8)

        self.assertEqual(
            NativeContext.crack(ctx, 0, 0, 4, 0, False),
            (CrackStatus.BAD_SEARCH_LENGTH, None),
        )
        self.assertEqual(
            NativeContext.crack(ctx, 0, 0, 4, 0, False, None),
            (CrackStatus.BAD_SEARCH_LENGTH, None),
        )

        with self.assertRaises(TypeError):
            NativeContext.crack(ctx, 0, 8, 4, 0, False, None, 0)

    def test_native_method_validates_target_range(self):
        small_ctx = CrackContext(bit_length=8, offset_basis=0, prime=1)
        with self.assertRaisesRegex(OverflowError, "target"):
            NativeContext.crack(small_ctx, 256, 1, 4, 0, False)

        fmpz_ctx = CrackContext(bit_length=128)
        with self.assertRaisesRegex(ValueError, "target"):
            NativeContext.crack(fmpz_ctx, -1, 1, 4, 0, False)

        with self.assertRaisesRegex(OverflowError, "target"):
            NativeContext.crack(fmpz_ctx, 2**128, 1, 4, 0, False)

    def test_native_method_rejects_total_length_overflow(self):
        ctx = CrackContext(prefix=b"x")
        with self.assertRaisesRegex(OverflowError, "crack_len"):
            NativeContext.crack(ctx, 0, 2**32 - 1, 4, 0, False)
