import os
import select
import signal
import subprocess
import sys
import textwrap
import unittest

from conftest import run_python


class InterruptHandlingTestCase(unittest.TestCase):
    def test_python_crack_handles_sigint_from_parent_process(self):
        code = """
            import sys
            import threading

            from fnvcrack import CrackContext

            sys.setswitchinterval(1000.0)
            gate = threading.Lock()
            gate.acquire()
            notifier_ready = threading.Event()

            def announce_acquisition():
                notifier_ready.set()
                with gate:
                    print("ready", flush=True)

            ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
            notifier = threading.Thread(target=announce_acquisition)
            notifier.start()
            assert notifier_ready.wait(timeout=5)

            gate.release()
            try:
                ctx.crack(0x1234567890abcdef, crack_len=12)
            except KeyboardInterrupt:
                notifier.join(timeout=5)
                assert not notifier.is_alive()
                print("python interrupt ok")
                raise SystemExit(0)

            raise SystemExit("python interrupt missed")
        """

        proc = subprocess.Popen(
            [sys.executable, "-c", textwrap.dedent(code)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert proc.stdout is not None
        ready, _, _ = select.select([proc.stdout], [], [], 10)
        if not ready:
            proc.kill()
            stdout, stderr = proc.communicate()
            self.fail(f"subprocess did not become ready\nstdout={stdout}\nstderr={stderr}")
        self.assertEqual(proc.stdout.readline().strip(), "ready")

        os.kill(proc.pid, signal.SIGINT)
        try:
            stdout, stderr = proc.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate()
            self.fail(f"subprocess timed out\nstdout={stdout}\nstderr={stderr}")

        self.assertEqual(proc.returncode, 0, stderr)
        self.assertIn("python interrupt ok", stdout)

    def test_gated_default_handler_interrupts_and_context_can_be_reused(self):
        returncode, stdout, stderr = run_python(
            """
            import os
            import signal
            import sys
            import threading

            from fnvcrack import CrackContext

            sys.setswitchinterval(1000.0)
            gate = threading.Lock()
            gate.acquire()
            sender_ready = threading.Event()

            def interrupt():
                sender_ready.set()
                with gate:
                    os.kill(os.getpid(), signal.SIGINT)

            def fnv(data):
                value = 0xcbf29ce484222325
                for byte in data:
                    value ^= byte
                    value *= 0x100000001b3
                    value &= (1 << 64) - 1
                return value

            ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
            sender = threading.Thread(target=interrupt)
            sender.start()
            assert sender_ready.wait(timeout=5)

            gate.release()
            try:
                ctx.crack(0x1234567890abcdef, crack_len=12)
            except KeyboardInterrupt:
                pass
            else:
                raise SystemExit("crack did not raise KeyboardInterrupt")

            sender.join(timeout=5)
            assert not sender.is_alive()

            result = ctx.crack(fnv(b"abcdefgh"), crack_len=8)
            assert result.ok
            print(result.value.decode())
            """
        )

        self.assertEqual(returncode, 0, stderr)
        self.assertIn("abcdefgh", stdout)

    def test_gated_custom_handler_returns_interrupted_status(self):
        returncode, stdout, stderr = run_python(
            """
            import os
            import signal
            import sys
            import threading

            from fnvcrack import CrackContext, CrackStatus

            sys.setswitchinterval(1000.0)
            gate = threading.Lock()
            gate.acquire()
            sender_ready = threading.Event()
            received = []

            def handler(signum, frame):
                received.append(signum)

            def interrupt():
                sender_ready.set()
                with gate:
                    os.kill(os.getpid(), signal.SIGINT)

            signal.signal(signal.SIGINT, handler)
            ctx = CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz")
            sender = threading.Thread(target=interrupt)
            sender.start()
            assert sender_ready.wait(timeout=5)

            gate.release()
            result = ctx.crack(0x1234567890abcdef, crack_len=12)
            sender.join(timeout=5)

            assert not sender.is_alive()
            assert received == [signal.SIGINT]
            assert result.status == CrackStatus.INTERRUPTED
            assert result.value is None
            print("custom handler ok")
            """
        )

        self.assertEqual(returncode, 0, stderr)
        self.assertIn("custom handler ok", stdout)

    def test_gated_custom_exception_propagates_from_fmpz_solve(self):
        returncode, stdout, stderr = run_python(
            """
            import os
            import signal
            import sys
            import threading

            from fnvcrack import CrackContext

            OFFSET_BASIS = int("6c62272e07bb014262b821756295c58d", 16)
            PRIME = int("0000000001000000000000000000013b", 16)

            class CustomInterrupt(Exception):
                pass

            sys.setswitchinterval(1000.0)
            gate = threading.Lock()
            gate.acquire()
            sender_ready = threading.Event()

            def handler(signum, frame):
                raise CustomInterrupt("custom signal exception")

            def interrupt():
                sender_ready.set()
                with gate:
                    os.kill(os.getpid(), signal.SIGINT)

            signal.signal(signal.SIGINT, handler)
            ctx = CrackContext(
                offset_basis=OFFSET_BASIS,
                prime=PRIME,
                bit_length=128,
                valid_chars=b"abcdefghijklmnopqrstuvwxyz",
            )
            sender = threading.Thread(target=interrupt)
            sender.start()
            assert sender_ready.wait(timeout=5)

            gate.release()
            try:
                ctx.crack(0x1234567890abcdef1234567890abcdef, crack_len=12)
            except CustomInterrupt as exc:
                assert str(exc) == "custom signal exception"
            else:
                raise SystemExit("custom signal exception was not propagated")

            sender.join(timeout=5)
            assert not sender.is_alive()
            print("custom exception ok")
            """
        )

        self.assertEqual(returncode, 0, stderr)
        self.assertIn("custom exception ok", stdout)

    def test_overlapping_worker_cracks_share_process_interrupt(self):
        returncode, stdout, stderr = run_python(
            """
            import os
            import signal
            import sys
            import threading

            from fnvcrack import CrackContext, CrackStatus

            sys.setswitchinterval(1000.0)
            contexts = [
                CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz"),
                CrackContext(valid_chars=b"abcdefghijklmnopqrstuvwxyz"),
            ]
            ready = [threading.Event(), threading.Event()]
            results = []

            def solve(index):
                ready[index].set()
                result = contexts[index].crack(
                    0x1234567890abcdef + index,
                    crack_len=12,
                )
                results.append(result)

            workers = [
                threading.Thread(target=solve, args=(0,)),
                threading.Thread(target=solve, args=(1,)),
            ]
            workers[0].start()
            assert ready[0].wait(timeout=5)
            workers[1].start()
            assert ready[1].wait(timeout=5)
            assert all(worker.is_alive() for worker in workers)

            caught = False
            try:
                os.kill(os.getpid(), signal.SIGINT)
            except KeyboardInterrupt:
                caught = True

            for worker in workers:
                worker.join(timeout=10)

            assert caught
            assert all(not worker.is_alive() for worker in workers)
            assert len(results) == 2
            assert all(result.status == CrackStatus.INTERRUPTED for result in results)
            assert all(result.value is None for result in results)
            print("overlap ok")
            """,
            timeout=20,
        )

        self.assertEqual(returncode, 0, stderr)
        self.assertIn("overlap ok", stdout)
