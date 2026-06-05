import os
import signal
import subprocess
import sys
import textwrap
import time
import unittest

from conftest import run_python


class InterruptHandlingTestCase(unittest.TestCase):
    def test_python_crack_handles_sigint_from_parent_process(self):
        code = """
            from fnvcrack import CrackContext, CrackOptions

            ctx = CrackContext(
                valid_chars=b"abcdefghijklmnopqrstuvwxyz",
                brute_chars=b"abcdefghijklmnopqrstuvwxyz",
            )

            try:
                ctx.crack(
                    0x1234567890abcdef,
                    max_len=12,
                    options=CrackOptions(max_crack_len=8),
                )
            except KeyboardInterrupt:
                print("python interrupt ok")
                raise SystemExit(0)

            print("python interrupt missed")
            raise SystemExit(1)
        """

        proc = subprocess.Popen(
            [sys.executable, "-c", textwrap.dedent(code)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        time.sleep(0.2)
        os.kill(proc.pid, signal.SIGINT)
        stdout, stderr = proc.communicate(timeout=10)

        self.assertEqual(proc.returncode, 0)
        self.assertIn("python interrupt ok", stdout)
        self.assertIn("fnvcrack: interrupt requested", stderr)

    def test_python_crack_releases_gil_during_native_solve(self):
        returncode, stdout, stderr = run_python(
            """
            import os
            import signal
            import threading
            import time

            from fnvcrack import CrackContext, CrackOptions

            ctx = CrackContext(
                valid_chars=b"abcdefghijklmnopqrstuvwxyz",
                brute_chars=b"abcdefghijklmnopqrstuvwxyz",
            )

            def interrupt():
                time.sleep(0.1)
                print("thread ran", flush=True)
                os.kill(os.getpid(), signal.SIGINT)

            thread = threading.Thread(target=interrupt)
            thread.start()

            try:
                ctx.crack(
                    0x1234567890abcdef,
                    max_len=12,
                    options=CrackOptions(max_crack_len=8),
                )
            except KeyboardInterrupt:
                thread.join(timeout=1)
                print("gil released")
                raise SystemExit(0)

            print("gil was not released")
            raise SystemExit(1)
            """
        )

        self.assertEqual(returncode, 0)
        self.assertIn("thread ran", stdout)
        self.assertIn("gil released", stdout)
        self.assertIn("fnvcrack: interrupt requested", stderr)

    def test_repeated_interrupts_do_not_poison_later_cracks(self):
        returncode, stdout, stderr = run_python(
            """
            import os
            import signal
            import threading
            import time

            from fnvcrack import CrackContext, CrackOptions

            def fnv(data):
                hsh = 0xcbf29ce484222325
                for c in data:
                    hsh ^= c
                    hsh *= 0x100000001b3
                    hsh &= (1 << 64) - 1
                return hsh

            ctx = CrackContext(
                valid_chars=b"abcdefghijklmnopqrstuvwxyz",
                brute_chars=b"abcdefghijklmnopqrstuvwxyz",
            )

            def interrupt():
                time.sleep(0.1)
                os.kill(os.getpid(), signal.SIGINT)

            thread = threading.Thread(target=interrupt)
            thread.start()
            try:
                ctx.crack(0x1234567890abcdef, max_len=12, options=CrackOptions(max_crack_len=8))
            except KeyboardInterrupt:
                thread.join(timeout=1)
            else:
                raise SystemExit("first crack was not interrupted")

            result = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz").crack(fnv(b"abcdefgh"), max_len=8)
            print(result.ok)
            raise SystemExit(0 if result.ok else 1)
            """
        )

        self.assertEqual(returncode, 0)
        self.assertIn("True", stdout)
        self.assertIn("fnvcrack: interrupt requested", stderr)
