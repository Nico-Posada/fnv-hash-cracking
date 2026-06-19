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

    def test_failed_bit_length_constructors_do_not_leak(self):
        returncode, stdout, stderr = run_python(
            """
            import resource

            from fnvcrack import CrackContext

            with open("/proc/self/statm") as f:
                pages = int(f.read().split()[0])

            current = pages * resource.getpagesize()
            soft = current + 32 * 1024 * 1024
            _, hard = resource.getrlimit(resource.RLIMIT_AS)
            if hard != resource.RLIM_INFINITY:
                soft = min(soft, hard)

            resource.setrlimit(resource.RLIMIT_AS, (soft, hard))

            for _ in range(500000):
                try:
                    CrackContext(bit_length=0)
                except ValueError:
                    pass

            print("ok")
            """
        )

        self.assertEqual(returncode, 0)
        self.assertEqual(stdout.strip(), "ok")
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

    def test_large_bit_length_target_validation_does_not_allocate_modulus(self):
        returncode, stdout, stderr = run_python(
            """
            import resource

            from fnvcrack import CrackContext

            ctx = CrackContext(offset_basis=0, prime=1, bit_length=500000000)

            with open("/proc/self/statm") as f:
                pages = int(f.read().split()[0])

            current = pages * resource.getpagesize()
            soft = current + 16 * 1024 * 1024
            _, hard = resource.getrlimit(resource.RLIMIT_AS)
            if hard != resource.RLIM_INFINITY:
                soft = min(soft, hard)

            resource.setrlimit(resource.RLIMIT_AS, (soft, hard))
            result = ctx.crack(0, max_len=0)
            print(result.status_name)
            """
        )

        self.assertEqual(returncode, 0)
        self.assertEqual(stdout.strip(), "BAD_SEARCH_LENGTH")
        self.assertEqual(stderr, "")
