import unittest

from fnvcrack import CrackContext, CrackOptions, CrackStatus, CrackStrategy

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
    def test_enumerate_cracks_common_charsets(self):
        cases = [
            (b"abcdefgh", LOWER),
            (b"abc12345", ALNUM),
            (b"Az 19!~?", PRINTABLE),
            (
                bytes(
                    [
                        0x00,
                        0x01,
                        0x80,
                        0xff,
                        ord("A"),
                        ord("b"),
                        0x7f,
                        0x20,
                    ]
                ),
                FULL_BYTES,
            ),
        ]

        for plaintext, charset in cases:
            with self.subTest(plaintext=plaintext, charset_len=len(charset)):
                ctx = CrackContext(valid_chars=charset)
                target = fnv(plaintext)
                result = ctx.crack(
                    target,
                    max_len=len(plaintext),
                    options=CrackOptions(strategy=CrackStrategy.ENUMERATE),
                )
                self.assertCracked(result, target, ctx)

    def test_lll_default_wrapper_still_cracks_basic_case(self):
        plaintext = b"abcdefgh"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)
        result = ctx.crack(target, max_len=8)
        self.assertCracked(result, target, ctx)

    def test_enumerate_cracks_known_case_where_lll_misses(self):
        plaintext = b"zspsevwr"
        target = fnv(plaintext)
        ctx = CrackContext(valid_chars=LOWER)

        lll_result = ctx.crack(target, max_len=8)
        self.assertEqual(lll_result.status, CrackStatus.FAILED)
        self.assertIsNone(lll_result.value)

        enum_result = ctx.crack(
            target,
            max_len=8,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE),
        )
        self.assertEqual(enum_result.value, plaintext)

    def test_enumerate_with_prefix_suffix_and_bruteforce(self):
        plaintext = b"preaaabcdefsuf"
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=b"pre",
            suffix=b"suf",
            brute_chars=LOWER,
            valid_chars=LOWER,
        )
        result = ctx.crack(
            target,
            max_len=11,
            options=CrackOptions(
                strategy=CrackStrategy.ENUMERATE,
                max_crack_len=6,
            ),
        )
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_lll_with_prefix_suffix_and_bruteforce(self):
        plaintext = b"preaaabcdefghsuf"
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=b"pre",
            suffix=b"suf",
            brute_chars=LOWER,
            valid_chars=LOWER,
        )
        result = ctx.crack(
            target,
            max_len=12,
            options=CrackOptions(max_crack_len=8),
        )
        self.assertCracked(result, target, ctx)

    def test_enumerate_bruteforce_accepts_nul_bytes(self):
        plaintext = b"pre\x00abcdef"
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=b"pre",
            brute_chars=b"\x00a",
            valid_chars=LOWER,
        )
        result = ctx.crack(
            target,
            max_len=7,
            options=CrackOptions(
                strategy=CrackStrategy.ENUMERATE,
                max_crack_len=6,
            ),
        )
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_enumerate_can_fully_bruteforce_unknown_bytes(self):
        plaintext = b"prebsuf"
        ctx = CrackContext(
            prefix=b"pre",
            suffix=b"suf",
            brute_chars=b"ab",
            valid_chars=b"xyz",
        )
        target = fnv(plaintext)
        result = ctx.crack(
            target,
            max_len=1,
            options=CrackOptions(
                strategy=CrackStrategy.ENUMERATE,
                max_crack_len=0,
            ),
        )
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_exact_known_string_with_zero_unknown_length(self):
        plaintext = b"prefixsuffix"
        ctx = CrackContext(prefix=b"prefix", suffix=b"suffix")
        result = ctx.crack(
            fnv(plaintext),
            max_len=0,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE, max_crack_len=0),
        )
        self.assertEqual(result.value, plaintext)

    def test_wrong_hash_for_known_string_fails_without_output(self):
        ctx = CrackContext(prefix=b"prefix", suffix=b"suffix")
        result = ctx.crack(
            0,
            max_len=0,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE, max_crack_len=0),
        )
        self.assertEqual(result.status, CrackStatus.FAILED)
        self.assertIsNone(result.value)

    def test_search_across_lengths_finds_shorter_match(self):
        plaintext = b"abc"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)
        result = ctx.crack(
            target,
            max_len=8,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE),
        )
        self.assertEqual(result.value, plaintext)

    def test_enum_bound_zero_is_not_rewritten_to_default(self):
        plaintext = b"axjshmti"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)

        zero_bound = ctx.crack(
            target,
            max_len=8,
            options=CrackOptions(
                strategy=CrackStrategy.ENUMERATE,
                enum_bound=0,
            ),
        )
        default_bound = ctx.crack(
            target,
            max_len=8,
            options=CrackOptions(
                strategy=CrackStrategy.ENUMERATE,
                enum_bound=4,
            ),
        )

        self.assertEqual(zero_bound.status, CrackStatus.FAILED)
        self.assertIsNone(zero_bound.value)
        self.assertEqual(default_bound.value, plaintext)

    def test_max_len_zero_is_bad_search_length(self):
        result = CrackContext().crack(0, max_len=0)
        self.assertEqual(result.status, CrackStatus.BAD_SEARCH_LENGTH)
        self.assertIsNone(result.value)

    def test_missing_brute_chars_reports_error(self):
        plaintext = b"aaabcdef"
        ctx = CrackContext(valid_chars=LOWER)
        result = ctx.crack(
            fnv(plaintext),
            max_len=8,
            options=CrackOptions(max_crack_len=6),
        )
        self.assertEqual(result.status, CrackStatus.MISSING_BRUTE_CHARS)
        self.assertIsNone(result.value)

    def test_bad_search_length_with_known_parts_reports_error(self):
        ctx = CrackContext(prefix=b"pre", suffix=b"suf")
        result = ctx.crack(
            0,
            max_len=5,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE),
        )
        self.assertEqual(result.status, CrackStatus.FAILED)

    def test_max_enum_candidates_limit_does_not_crash(self):
        ctx = CrackContext(valid_chars=LOWER)
        result = ctx.crack(
            0,
            max_len=8,
            options=CrackOptions(
                strategy=CrackStrategy.ENUMERATE,
                max_enum_candidates=1,
            ),
        )
        self.assertIn(result.status, (CrackStatus.FAILED, CrackStatus.SUCCESS))

    def test_non_matching_charset_fails_without_false_positive(self):
        plaintext = b"abcdefgh"
        ctx = CrackContext(valid_chars=DIGITS)
        result = ctx.crack(
            fnv(plaintext),
            max_len=8,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE),
        )
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
        result = ctx.crack(
            target,
            max_len=4,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE),
        )
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
        result = ctx.crack(
            target,
            max_len=2,
            options=CrackOptions(
                strategy=CrackStrategy.ENUMERATE,
                max_crack_len=2,
            ),
        )
        self.assertCracked(result, target, ctx)

    def test_fmpz_lll_default_path(self):
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
        result = ctx.crack(
            target,
            max_len=8,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE),
        )
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
        result = ctx.crack(
            target,
            max_len=0,
            options=CrackOptions(strategy=CrackStrategy.ENUMERATE, max_crack_len=0),
        )
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)
