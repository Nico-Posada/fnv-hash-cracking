import itertools
import threading
import time
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

COLLISION_VALUES = {b"ga", b"ic", b"jz", b"se"}


def collision_context():
    return CrackContext(offset_basis=0x25, prime=0xb3, bit_length=8, valid_chars=LOWER)


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
                result = ctx.crack(target, crack_len=len(plaintext))
                self.assertCracked(result, target, ctx)

    def test_default_cracks_case_the_legacy_path_missed(self):
        plaintext = b"zspsevwr"
        ctx = CrackContext(valid_chars=LOWER)
        result = ctx.crack(fnv(plaintext), crack_len=8)
        self.assertEqual(result.value, plaintext)

    def test_prefix_suffix_unknown_bytes(self):
        plaintext = b"preaaabcdefsuf"
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=b"pre",
            suffix=b"suf",
            valid_chars=LOWER,
        )
        result = ctx.crack(target, crack_len=8)
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
        result = ctx.crack(target, crack_len=4)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_accepts_nul_bytes_in_unknown(self):
        plaintext = b"pre\x00abc"
        target = fnv(plaintext)
        ctx = CrackContext(
            prefix=b"pre",
            valid_chars=b"\x00" + LOWER,
        )
        result = ctx.crack(target, crack_len=4)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)
        threaded = ctx.crack(target, crack_len=4, threads=4)
        self.assertEqual(threaded.value, plaintext)
        self.assertCracked(threaded, target, ctx)

    def test_exact_known_string_with_zero_unknown_length(self):
        plaintext = b"prefixsuffix"
        ctx = CrackContext(prefix=b"prefix", suffix=b"suffix")
        result = ctx.crack(fnv(plaintext), crack_len=0)
        self.assertEqual(result.value, plaintext)
        threaded = ctx.crack(fnv(plaintext), crack_len=0, threads=4)
        self.assertEqual(threaded.value, plaintext)

    def test_wrong_hash_for_known_string_fails_without_output(self):
        ctx = CrackContext(prefix=b"prefix", suffix=b"suffix")
        result = ctx.crack(0, crack_len=0)
        self.assertEqual(result.status, CrackStatus.FAILED)
        self.assertIsNone(result.value)

    def test_default_requires_exact_unknown_length(self):
        plaintext = b"abc"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)

        too_long = ctx.crack(target, crack_len=8)
        exact = ctx.crack(target, crack_len=3)

        self.assertEqual(too_long.status, CrackStatus.FAILED)
        self.assertIsNone(too_long.value)
        self.assertEqual(exact.value, plaintext)

    def test_incremental_finds_shorter_match(self):
        plaintext = b"abc"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)
        result = ctx.crack(target, crack_len=8, incremental=True)
        self.assertEqual(result.value, plaintext)

    def test_threaded_prefix_suffix_crack(self):
        plaintext = b"preabcsuf"
        ctx = CrackContext(prefix=b"pre", suffix=b"suf", valid_chars=LOWER)
        target = fnv(plaintext)

        result = ctx.crack(target, crack_len=3, threads=4)

        self.assertCracked(result, target, ctx)

    def test_threaded_incremental_finds_shorter_match(self):
        plaintext = b"abc"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)

        result = ctx.crack(target, crack_len=8, incremental=True, threads=4)

        self.assertEqual(result.value, plaintext)

    def test_threaded_single_character_charset(self):
        plaintext = b"aaaa"
        ctx = CrackContext(valid_chars=b"a")
        target = fnv(plaintext)

        result = ctx.crack(target, crack_len=4, threads=8)

        self.assertEqual(result.value, plaintext)

    def test_batch_crack(self):
        ctx = CrackContext(prefix=b"pre", suffix=b"suf", valid_chars=b"ab")
        targets = (
            target
            for target in (
                fnv(b"preasuf"),
                0,
                fnv(b"prebbsuf"),
            )
        )

        self.assertEqual(
            ctx.batch_crack(targets, crack_len=2, incremental=True, processes=1),
            [b"preasuf", None, b"prebbsuf"],
        )
        self.assertEqual(ctx.batch_crack([], crack_len=2), [])

    def test_prefix_suffix_crack_len_is_unknown_length(self):
        plaintext = b"preabcsuf"
        target = fnv(plaintext)
        ctx = CrackContext(prefix=b"pre", suffix=b"suf", valid_chars=LOWER)

        too_long = ctx.crack(target, crack_len=4)
        exact = ctx.crack(target, crack_len=3)

        self.assertEqual(too_long.status, CrackStatus.FAILED)
        self.assertIsNone(too_long.value)
        self.assertEqual(exact.value, plaintext)

    def test_enum_bound_zero_is_not_rewritten_to_default(self):
        plaintext = b"axjshmti"
        ctx = CrackContext(valid_chars=LOWER)
        target = fnv(plaintext)

        zero_bound = ctx.crack(
            target,
            crack_len=8,
            enum_bound=0,
        )
        default_bound = ctx.crack(
            target,
            crack_len=8,
            enum_bound=4,
        )

        self.assertEqual(zero_bound.status, CrackStatus.FAILED)
        self.assertIsNone(zero_bound.value)
        self.assertEqual(default_bound.value, plaintext)

    def test_crack_len_zero_is_bad_search_length(self):
        result = CrackContext().crack(0, crack_len=0)
        self.assertEqual(result.status, CrackStatus.BAD_SEARCH_LENGTH)
        self.assertIsNone(result.value)

    def test_bad_search_length_with_known_parts_reports_error(self):
        ctx = CrackContext(prefix=b"pre", suffix=b"suf")
        result = ctx.crack(0, crack_len=5)
        self.assertEqual(result.status, CrackStatus.FAILED)

    def test_max_enum_candidates_limit_does_not_crash(self):
        ctx = CrackContext(valid_chars=LOWER)
        for threads in (1, 4):
            with self.subTest(threads=threads):
                result = ctx.crack(
                    0,
                    crack_len=8,
                    max_enum_candidates=1,
                    threads=threads,
                )
                self.assertIn(result.status, (CrackStatus.FAILED, CrackStatus.SUCCESS))

    def test_non_matching_charset_fails_without_false_positive(self):
        plaintext = b"abcdefgh"
        ctx = CrackContext(valid_chars=DIGITS)
        result = ctx.crack(fnv(plaintext), crack_len=8)
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
        result = ctx.crack(target, crack_len=4)
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
        result = ctx.crack(target, crack_len=2)
        self.assertCracked(result, target, ctx)

    def test_small_width_bounds_keep_every_reachable_target(self):
        allowed = b"\x01\x06"
        ctx = CrackContext(
            offset_basis=5,
            prime=11,
            bit_length=4,
            prefix=b"\x0e",
            suffix=b"\x0d",
            valid_chars=allowed + b"\x11",
        )

        for unknown in itertools.product(allowed, repeat=3):
            plaintext = ctx.prefix + bytes(unknown) + ctx.suffix
            target = fnv(plaintext, ctx.offset_basis, ctx.prime, ctx.bit_length)
            with self.subTest(unknown=unknown, target=target):
                result = ctx.crack(target, crack_len=3, enum_bound=15)
                self.assertCracked(result, target, ctx)

    def test_callback_rejects_then_accepts_verified_candidate(self):
        ctx = collision_context()
        seen = []

        def accept_second(candidate):
            seen.append(candidate)
            return len(seen) == 2

        result = ctx.crack(0xa5, crack_len=2, enum_bound=255, callback=accept_second)

        self.assertEqual(len(seen), 2)
        self.assertNotEqual(seen[0], seen[1])
        self.assertTrue(all(fnv(value, 0x25, 0xb3, 8) == 0xa5 for value in seen))
        self.assertEqual(result.status, CrackStatus.SUCCESS)
        self.assertEqual(result.value, seen[1])

    def test_threaded_callback_calls_are_fully_serialized(self):
        ctx = collision_context()
        active = 0
        max_active = 0
        seen = []
        state_lock = threading.Lock()

        def reject(candidate):
            nonlocal active, max_active
            with state_lock:
                active += 1
                max_active = max(max_active, active)
            time.sleep(0.01)
            seen.append(candidate)
            with state_lock:
                active -= 1
            return False

        result = ctx.crack(0xa5, crack_len=2, enum_bound=255, callback=reject, threads=4)

        self.assertEqual(max_active, 1)
        self.assertGreaterEqual(len(set(seen)), 2)
        self.assertEqual(result.status, CrackStatus.FAILED)

    def test_threaded_callback_acceptance_stops_later_calls(self):
        ctx = collision_context()
        seen = []

        def accept(candidate):
            seen.append(candidate)
            time.sleep(0.01)
            return True

        result = ctx.crack(0xa5, crack_len=2, enum_bound=255, callback=accept, threads=4)

        self.assertEqual(seen, [result.value])
        self.assertEqual(result.status, CrackStatus.SUCCESS)

    def test_threaded_callback_exception_propagates_unchanged(self):
        class SentinelError(Exception):
            pass

        sentinel = SentinelError("sentinel")

        def fail(_candidate):
            raise sentinel

        with self.assertRaises(SentinelError) as caught:
            collision_context().crack(0xa5, crack_len=2, enum_bound=255, callback=fail, threads=4)

        self.assertIs(caught.exception, sentinel)

    def test_callback_can_collect_until_bounded_exhaustion(self):
        ctx = collision_context()
        candidates = []
        for threads in (1, 4):
            seen = []
            result = ctx.crack(
                0xa5,
                crack_len=2,
                enum_bound=255,
                callback=lambda candidate: seen.append(candidate) and False,
                threads=threads,
            )
            self.assertEqual(len(seen), len(set(seen)))
            self.assertTrue(all(fnv(value, 0x25, 0xb3, 8) == 0xa5 for value in seen))
            self.assertEqual(set(seen), COLLISION_VALUES)
            self.assertEqual(result.status, CrackStatus.FAILED)
            self.assertIsNone(result.value)
            candidates.append(set(seen))
        self.assertEqual(candidates[0], candidates[1])

    def test_callback_can_reject_only_known_candidate(self):
        ctx = CrackContext(prefix=b"prefix", suffix=b"suffix")
        seen = []

        result = ctx.crack(
            fnv(b"prefixsuffix"),
            crack_len=0,
            callback=lambda candidate: seen.append(candidate) and False,
        )

        self.assertEqual(seen, [b"prefixsuffix"])
        self.assertEqual(result.status, CrackStatus.FAILED)
        self.assertIsNone(result.value)

    def test_callback_incremental_search_continues_after_rejection(self):
        ctx = collision_context()
        seen = []

        def accept_length_two(candidate):
            seen.append(candidate)
            return len(candidate) == 2

        result = ctx.crack(
            0xa5,
            crack_len=2,
            enum_bound=255,
            incremental=True,
            callback=accept_length_two,
        )

        self.assertEqual(seen[0], b"b")
        self.assertIn(result.value, COLLISION_VALUES)
        self.assertEqual(result.status, CrackStatus.SUCCESS)

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
        result = ctx.crack(target, crack_len=4)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_fmpz_callback_accepts_verified_candidate(self):
        plaintext = b"abcd"
        offset_basis = int("6c62272e07bb014262b821756295c58d", 16)
        prime = int("0000000001000000000000000000013b", 16)
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=128,
            valid_chars=LOWER,
        )
        seen = []

        result = ctx.crack(
            fnv(plaintext, offset_basis, prime, 128),
            crack_len=4,
            callback=lambda candidate: seen.append(candidate) or True,
        )

        self.assertEqual(seen, [plaintext])
        self.assertEqual(result.status, CrackStatus.SUCCESS)
        self.assertEqual(result.value, plaintext)

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
        result = ctx.crack(target, crack_len=8)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

    def test_wide_fmpz_enumerate_path(self):
        plaintext = b"ab"
        offset_basis = (1 << 255) + 0x12345
        prime = (1 << 168) + 0x163
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=256,
            valid_chars=LOWER,
        )
        target = fnv(plaintext, offset_basis, prime, 256)
        result = ctx.crack(target, crack_len=2)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)

        seen = []
        threaded = ctx.crack(
            target,
            crack_len=2,
            callback=lambda candidate: seen.append(candidate) or True,
            threads=4,
        )
        self.assertEqual(seen, [plaintext])
        self.assertEqual(threaded.value, plaintext)
        self.assertCracked(threaded, target, ctx)

    def test_fmpz_incremental_path(self):
        plaintext = b"abc"
        offset_basis = int("6c62272e07bb014262b821756295c58d", 16)
        prime = int("0000000001000000000000000000013b", 16)
        ctx = CrackContext(
            offset_basis=offset_basis,
            prime=prime,
            bit_length=128,
            valid_chars=LOWER,
        )
        target = fnv(plaintext, offset_basis, prime, 128)

        too_long = ctx.crack(target, crack_len=4)
        incremental = ctx.crack(target, crack_len=4, incremental=True)

        self.assertEqual(too_long.status, CrackStatus.FAILED)
        self.assertIsNone(too_long.value)
        self.assertEqual(incremental.value, plaintext)
        self.assertCracked(incremental, target, ctx)

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
        result = ctx.crack(target, crack_len=0)
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
        result = ctx.crack(target, crack_len=0)
        self.assertEqual(result.value, plaintext)
        self.assertCracked(result, target, ctx)
