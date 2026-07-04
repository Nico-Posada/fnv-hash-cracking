import importlib.util
import json
import statistics
import subprocess
import tempfile
import unittest
import warnings
from pathlib import Path
from unittest import mock

from fnvcrack import FNV64_OFFSET_BASIS, FNV64_PRIME


BENCH_PATH = Path(__file__).resolve().parents[1] / "benchmarking" / "bench.py"
SPEC = importlib.util.spec_from_file_location("bench_cli", BENCH_PATH)
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)


class BenchmarkCliTestCase(unittest.TestCase):
    def write_speedscope(self, path):
        path.write_text(
            json.dumps(
                {
                    "shared": {
                        "frames": [
                            {"name": "root", "file": "bench.py", "line": 1},
                            {"name": "native_work", "file": "crack.c", "line": 2},
                            {"name": "leaf", "file": "enumerate.c", "line": 3},
                        ],
                    },
                    "profiles": [
                        {
                            "type": "sampled",
                            "name": "Thread",
                            "unit": "seconds",
                            "startValue": 0,
                            "endValue": 0.05,
                            "samples": [[0, 1], [0, 1, 2]],
                            "weights": [0.03, 0.02],
                        },
                    ],
                }
            ),
            encoding="ascii",
        )

    def parse_minimal(self, *extra, mode="perf"):
        return bench.parse_args([
            mode,
            "-p",
            "[a]",
            "-c",
            "[a]",
            "-n",
            "1",
            "-b",
            "4",
            "-m",
            "0",
            "-I",
            *extra,
        ])

    def test_required_arguments(self):
        with self.assertRaises(SystemExit):
            bench.parse_args(["perf"])

    def test_short_flags_parse_defaults(self):
        args = self.parse_minimal("-s", "123")
        self.assertEqual(args.pattern, "[a]")
        self.assertEqual(args.valid_chars_pattern, "[a]")
        self.assertEqual(args.count, 1)
        self.assertEqual(args.enum_bound, 4)
        self.assertEqual(args.max_enum_candidates, 0)
        self.assertFalse(args.incremental)
        self.assertEqual(args.offset_basis, FNV64_OFFSET_BASIS)
        self.assertEqual(args.prime, FNV64_PRIME)
        self.assertEqual(args.bit_length, 64)
        self.assertEqual(args.seed, 123)
        self.assertEqual(args.detail, "summary")

    def test_hex_integer_flags_parse(self):
        args = self.parse_minimal("-o", "0xff", "-r", "0xb3")
        self.assertEqual(args.offset_basis, 255)
        self.assertEqual(args.prime, 179)

    def test_random_seed_is_recorded(self):
        args = self.parse_minimal()
        result = bench.run_perf(args)
        self.assertIsInstance(result["config"]["seed"], int)

    def test_explicit_seed_is_deterministic(self):
        lhs = self.parse_minimal("-p", "[a-c]{2}", "-n", "4", "-s", "7")
        rhs = self.parse_minimal("-p", "[a-c]{2}", "-n", "4", "-s", "7")
        self.assertEqual(list(bench.generate_cases(lhs)), list(bench.generate_cases(rhs)))

    def test_valid_chars_pattern(self):
        self.assertEqual(bench.valid_chars_from_pattern("[a-c]"), b"abc")

    def test_escaped_valid_chars_pattern_can_match_all_bytes(self):
        self.assertEqual(
            bench.valid_chars_from_pattern(r"[\0-\xff]"),
            bytes(range(256)),
        )

    def test_escaped_nul_and_high_byte_generation(self):
        nul = self.parse_minimal("-p", r"\0", "-s", "1")
        high = self.parse_minimal("-p", r"\xff", "-s", "1")
        self.assertEqual(next(bench.generate_cases(nul))["unknown_hex"], "00")
        self.assertEqual(next(bench.generate_cases(high))["unknown_hex"], "ff")

    def test_regex_escapes_survive_stdlib_decode(self):
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            self.assertEqual(bench.decode_escapes(r"\d+\w"), r"\d+\w")

        self.assertFalse(caught)
        self.assertEqual(bench.decode_escapes(r"\1").encode("latin-1").hex(), "01")

    def test_rejects_multi_character_valid_matches(self):
        with self.assertRaisesRegex(bench.BenchError, "single bytes"):
            bench.valid_chars_from_pattern("[ab]{2}")

    def test_summary_includes_throughput(self):
        rows = [
            {
                "elapsed_seconds": 0.5,
                "success": True,
                "matched_original": True,
                "status_name": "SUCCESS",
            },
            {
                "elapsed_seconds": 1.0,
                "success": False,
                "matched_original": False,
                "status_name": "FAILED",
            },
        ]
        summary = bench.summarize(rows)
        self.assertEqual(summary["throughput_cases_per_second"], 2 / 1.5)

    def test_parse_speedscope_profile_extracts_function_samples(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "profile.speedscope.json"
            self.write_speedscope(path)

            profile = bench.parse_speedscope_profile(path)

        self.assertEqual(profile["sample_count"], 2)
        self.assertAlmostEqual(profile["sampled_seconds"], 0.05)
        self.assertEqual(profile["top_self_functions"][0]["name"], "native_work")
        self.assertAlmostEqual(profile["top_self_functions"][0]["seconds"], 0.03)
        self.assertEqual(profile["top_inclusive_functions"][0]["name"], "root")
        self.assertAlmostEqual(profile["top_inclusive_functions"][0]["seconds"], 0.05)

    def test_perf_json_smoke(self):
        args = self.parse_minimal("-j", "-s", "1")
        result = bench.run_perf(args)
        dumped = json.dumps(result)
        self.assertEqual(result["mode"], "perf")
        self.assertIn('"summary"', dumped)
        self.assertEqual(result["summary"]["count"], 1)

    def test_detail_controls_case_output(self):
        args = self.parse_minimal("-j", "-s", "1")
        result = bench.run_perf(args)
        self.assertNotIn("cases", bench.report(result, "summary"))
        self.assertIn("cases", bench.report(result, "full"))

    def test_suite_rollup_keeps_summary_percentiles_per_run(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "run.json"
            run = bench.run_perf(self.parse_minimal("-j", "-s", "1"))
            path.write_text(
                json.dumps(bench.report(run, "summary")),
                encoding="ascii",
            )
            (Path(tmpdir) / "suite.json").write_text(
                json.dumps({"mode": "suite"}),
                encoding="ascii",
            )

            args = bench.parse_args(["suite", tmpdir, "-j"])
            result = bench.run(args)

        self.assertEqual(result["mode"], "suite")
        self.assertEqual(result["summary"]["runs"], 1)
        self.assertEqual(result["summary"]["count"], run["summary"]["count"])
        self.assertEqual(result["summary"]["success"], run["summary"]["success"])
        self.assertIsNone(result["summary"]["median_seconds"])
        self.assertIsNone(result["summary"]["p95_seconds"])
        self.assertEqual(result["summary"]["percentiles_source"], "unavailable")
        self.assertEqual(
            result["summary"]["median_of_run_medians_seconds"],
            run["summary"]["median_seconds"],
        )
        self.assertEqual(result["runs"][0]["name"], "run")
        self.assertEqual(result["skipped"][0]["reason"], "suite result")
        self.assertTrue(result["runs"][0]["ok"])

    def test_suite_full_detail_reports_exact_percentiles(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "run.json"
            run = bench.run_perf(
                self.parse_minimal("-j", "-s", "2", "-p", "[a-c]{2}", "-n", "3")
            )
            path.write_text(json.dumps(run), encoding="ascii")

            result = bench.run(bench.parse_args(["suite", str(path), "-j"]))

        times = [case["elapsed_seconds"] for case in run["cases"]]
        self.assertEqual(result["summary"]["percentiles_source"], "cases")
        self.assertEqual(result["summary"]["median_seconds"], statistics.median(times))
        self.assertEqual(result["summary"]["p95_seconds"], bench.percentile(times, 95))

    def test_speedscope_without_svg_still_returns_structured_stats(self):
        args = self.parse_minimal("-j", "-s", "1", mode="speedscope")
        payload = json.dumps(bench.run_perf(args))

        def fake_run_py_spy(_args, output, output_format):
            self.assertEqual(output_format, "speedscope")
            self.write_speedscope(output)
            return subprocess.CompletedProcess([], 0, stdout=payload, stderr="")

        with mock.patch.object(bench, "run_py_spy", fake_run_py_spy):
            result = bench.run_speedscope(args)

        self.assertEqual(result["mode"], "speedscope")
        self.assertIn("summary", result)
        self.assertNotIn("svg_out", result["profiler"])
        self.assertEqual(result["profiler"]["top_self_functions"][0]["name"], "native_work")

    def test_speedscope_svg_failure_keeps_agent_profile(self):
        args = self.parse_minimal("-j", "-s", "1", "-g", "/tmp/ignored.svg", mode="speedscope")
        payload = json.dumps(bench.run_perf(args))

        def fake_run_py_spy(_args, output, output_format):
            if output_format == "speedscope":
                self.write_speedscope(output)
                return subprocess.CompletedProcess([], 0, stdout=payload, stderr="")
            return subprocess.CompletedProcess([], 1, stdout="", stderr="No child process")

        with mock.patch.object(bench, "run_py_spy", fake_run_py_spy):
            result = bench.run_speedscope(args)

        self.assertEqual(result["profiler"]["svg_out"], "/tmp/ignored.svg")
        self.assertEqual(result["profiler"]["svg_error"], "No child process")
        self.assertEqual(result["profiler"]["top_self_functions"][0]["name"], "native_work")


if __name__ == "__main__":
    unittest.main()
