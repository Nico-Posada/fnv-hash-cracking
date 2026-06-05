import unittest

from conftest import run_python


class SubprocessSafetyTestCase(unittest.TestCase):
    def test_repeated_construction_and_destruction_does_not_crash(self):
        returncode, stdout, stderr = run_python(
            """
            from fnvcrack import CrackContext

            for i in range(1000):
                CrackContext(
                    prefix=b"pre",
                    suffix=b"suf",
                    valid_chars=b"abcdefghijklmnopqrstuvwxyz",
                    brute_chars=b"abc",
                )

            print("ok")
            """
        )

        self.assertEqual(returncode, 0)
        self.assertEqual(stdout.strip(), "ok")
        self.assertEqual(stderr, "")

    def test_failed_constructors_do_not_crash_or_poison_future_contexts(self):
        returncode, stdout, stderr = run_python(
            """
            from fnvcrack import CrackContext

            for _ in range(100):
                try:
                    CrackContext(prefix="bad")
                except TypeError:
                    pass

                try:
                    CrackContext(bit_length=8, prime=256)
                except TypeError:
                    pass

            ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
            print(ctx.bit_length)
            """
        )

        self.assertEqual(returncode, 0)
        self.assertEqual(stdout.strip(), "64")
        self.assertEqual(stderr, "")

    def test_large_fmpz_conversion_does_not_crash(self):
        returncode, stdout, stderr = run_python(
            """
            from fnvcrack import CrackContext

            value = (1 << 4096) + 123
            ctx = CrackContext(offset_basis=value, prime=value - 2, bit_length=4097)
            print(ctx.offset_basis == value)
            print(ctx.prime == value - 2)
            """
        )

        self.assertEqual(returncode, 0)
        self.assertEqual(stdout.splitlines(), ["True", "True"])
        self.assertEqual(stderr, "")

    def test_bruteforce_product_oom_returns_memory_error(self):
        returncode, stdout, stderr = run_python(
            """
            import resource

            from fnvcrack import CrackContext, CrackOptions, CrackStrategy

            brute_chars = b"\\x00" * (256 * 1024)
            ctx = CrackContext(brute_chars=brute_chars, valid_chars=b"\\x00")

            with open("/proc/self/statm") as f:
                pages = int(f.read().split()[0])

            current = pages * resource.getpagesize()
            soft = current + 128 * 1024
            _, hard = resource.getrlimit(resource.RLIMIT_AS)
            if hard != resource.RLIM_INFINITY:
                soft = min(soft, hard)

            resource.setrlimit(resource.RLIMIT_AS, (soft, hard))
            result = ctx.crack(
                0,
                max_len=1,
                options=CrackOptions(
                    strategy=CrackStrategy.ENUMERATE,
                    max_crack_len=0,
                ),
            )
            print(result.status_name)
            """,
            timeout=10,
        )

        self.assertEqual(returncode, 0)
        self.assertEqual(stdout.strip(), "MEMORY_ERROR")
        self.assertEqual(stderr, "")
