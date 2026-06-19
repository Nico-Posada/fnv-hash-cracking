import unittest

from fnvcrack import CrackContext, CrackStatus

from conftest import (
    ALNUM,
    CrackAssertionsMixin,
    DIGITS,
    FULL_BYTES,
    LOWER,
    PRINTABLE,
    fnv,
)


class CrackingStrategiesTestCase(CrackAssertionsMixin, unittest.TestCase):
    def test_default_cracks_common_charsets(self):
        cases = [
            (b"abcdefgh", LOWER),
            (b"abc12345", ALNUM),
            (b"Az 19!~?", PRINTABLE),
            (
                bytes([
                    0x00,
                    0x01,
                    0x80,
                    0xff,
                    ord("A"),
                    ord("b"),
                    0x7f,
                    0x20,
                ]),
                FULL_BYTES,
            ),
        ]

        for plaintext, charset in cases:
            with self.subTest(plaintext=plaintext, charset_len=len(charset)):
                ctx = CrackContext(valid_chars=charset)
                target = fnv(plaintext)
                result = ctx.crack(target, max_len=len(plaintext))
                self.assertCracked(result, target, ctx)

    def test_default_cracks_case_the_legacy_path_missed(self):
        plaintext = b"zspsevwr"
        ctx = CrackContext(valid_chars=LOWER)
        result = ctx.crack(fnv(plaintext), max_len=8)
        self.assertEqual(result.value, plaintext)

    def test_prefix_suffix_unknown_bytes(self):
        plaintext = b"preaaabcdefsuf"
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=b"pre",
            suffix=b"suf",
            valid_chars=LOWER,
        )
        result = ctx.crack(target, max_len=8)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_preserves_nul_bytes_in_known_parts(self):
        prefix = b"pre\x00"
        suffix = b"\x00suf"
        plaintext = prefix + b"abcd" + suffix
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=prefix,
            suffix=suffix,
            valid_chars=LOWER,
        )
        result = ctx.crack(target, max_len=4)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_accepts_nul_bytes_in_unknown(self):
        plaintext = b"pre\x00abc"
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=b"pre",
            valid_chars=b"\x00" + LOWER,
        )
        result = ctx.crack(target, max_len=4)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_exact_known_string_with_zero_unknown_length(self):
        plaintext = b"prefixsuffix"
        ctx = CrackContext(prefix=b"prefix", suffix=b"suffix")
        result = ctx.crack(fnv(plaintext), max_len=0)
        self.assertEqual(result.value, plaintext)

    def test_wrong_hash_for_known_string_fails_without_output(self):
        ctx = CrackContext(prefix=b"prefix", suffix=b"suffix")
        result = ctx.crack(0, max_len=0)
        self.assertEqual(result.status, CrackStatus.FAILED)
        self.assertIsNone(result.value)

    def test_search_across_lengths_finds_shorter_match(self):
        plaintext = b"abc"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)
        result = ctx.crack(target, max_len=8)
        self.assertEqual(result.value, plaintext)

    def test_enum_bound_zero_is_not_rewritten_to_default(self):
        plaintext = b"axjshmti"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)

        zero_bound = ctx.crack(
            target,
            max_len=8,
            enum_bound=0,
        )
        default_bound = ctx.crack(
            target,
            max_len=8,
            enum_bound=4,
        )

        self.assertEqual(zero_bound.status, CrackStatus.FAILED)
        self.assertIsNone(zero_bound.value)
        self.assertEqual(default_bound.value, plaintext)

    def test_max_len_zero_is_bad_search_length(self):
        result = CrackContext().crack(0, max_len=0)
        self.assertEqual(result.status, CrackStatus.BAD_SEARCH_LENGTH)
        self.assertIsNone(result.value)

    def test_bad_search_length_with_known_parts_reports_error(self):
        ctx = CrackContext(prefix=b"pre", suffix=b"suf")
        result = ctx.crack(0, max_len=5)
        self.assertEqual(result.status, CrackStatus.FAILED)

    def test_max_enum_candidates_limit_does_not_crash(self):
        ctx = CrackContext(valid_chars=LOWER)
        result = ctx.crack(
            0,
            max_len=8,
            max_enum_candidates=1,
        )
        self.assertIn(result.status, (CrackStatus.FAILED, CrackStatus.SUCCESS))

    def test_non_matching_charset_fails_without_false_positive(self):
        plaintext = b"abcdefgh"
        ctx = CrackContext(valid_chars=DIGITS)
        result = ctx.crack(fnv(plaintext), max_len=8)
        self.assertEqual(result.status, CrackStatus.FAILED)
        self.assertIsNone(result.value)

    def test_32_bit_hash_path(self):
        plaintext = b"abcd"
        offset_basis = 0x811c9dc5
        prime = 0x01000193
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=32,
            valid_chars=LOWER,
        )
        target = fnv(plaintext, offset_basis, prime, 32)
        result = ctx.crack(target, max_len=4)
        self.assertCracked(result, target, ctx)

    def test_8_bit_hash_path(self):
        plaintext = b"az"
        offset_basis = 0x25
        prime = 0xb3
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=8,
            valid_chars=LOWER,
        )
        target = fnv(plaintext, offset_basis, prime, 8)
        result = ctx.crack(target, max_len=2)
        self.assertCracked(result, target, ctx)

    def test_fmpz_default_path(self):
        plaintext = b"abcd"
        offset_basis = int("6c62272e07bb014262b821756295c58d", 16)
        prime = int("0000000001000000000000000000013b", 16)
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=128,
            valid_chars=LOWER,
        )
        target = fnv(plaintext, offset_basis, prime, 128)
        result = ctx.crack(target, max_len=4)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_fmpz_enumerate_path(self):
        plaintext = b"abcd1234"
        offset_basis = int("6c62272e07bb014262b821756295c58d", 16)
        prime = int("0000000001000000000000000000013b", 16)
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=128,
            valid_chars=LOWER + DIGITS,
        )
        target = fnv(plaintext, offset_basis, prime, 128)
        result = ctx.crack(target, max_len=8)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_fmpz_exact_known_string_preserves_nul_bytes(self):
        prefix = b"\x00pre"
        suffix = b"suf\x00"
        plaintext = prefix + suffix
        offset_basis = int("6c62272e07bb014262b821756295c58d", 16)
        prime = int("0000000001000000000000000000013b", 16)
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=128,
            prefix=prefix,
            suffix=suffix,
        )
        target = fnv(plaintext, offset_basis, prime, 128)
        result = ctx.crack(target, max_len=0)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_fmpz_exact_known_string_accepts_high_bit_bytes(self):
        plaintext = bytes([0xff]) + b"suffix"
        offset_basis = int("6c62272e07bb014262b821756295c58d", 16)
        prime = int("0000000001000000000000000000013b", 16)
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=128,
            prefix=bytes([0xff]),
            suffix=b"suffix",
        )
        target = fnv(plaintext, offset_basis, prime, 128)
        result = ctx.crack(target, max_len=0)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)
