import argparse
import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MATRIX_PATH = Path(__file__).resolve().parents[1] / "benchmarking" / "matrix.py"
SPEC = importlib.util.spec_from_file_location("benchmark_matrix", MATRIX_PATH)
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)


class BenchmarkMatrixTestCase(unittest.TestCase):
    def write_results(self, root, seeds, scenario_ratios):
        baseline = root / "baseline"
        candidate = root / "candidate"
        baseline.mkdir()
        candidate.mkdir()
        for scenario_id, ratios in scenario_ratios.items():
            for seed, ratio in zip(seeds, ratios, strict=True):
                for repetition in range(1, matrix.REPETITIONS + 1):
                    name = f"{scenario_id}-seed{seed}-rep{repetition}.json"
                    config = {"scenario": scenario_id, "seed": seed}
                    case = {"index": 0, "status": 0, "elapsed_seconds": repetition / 100}
                    base_result = {
                        "config": config,
                        "environment": {"ignored": "baseline"},
                        "summary": {"total_seconds": 10.0, "ignored": repetition},
                        "slowest": [{"ignored": True}],
                        "cases": [case],
                    }
                    candidate_result = {
                        "config": config,
                        "environment": {"ignored": "candidate"},
                        "summary": {"total_seconds": 10.0 * ratio, "ignored": -repetition},
                        "slowest": [{"ignored": False}],
                        "cases": [{**case, "elapsed_seconds": repetition / 10}],
                    }
                    (baseline / name).write_text(json.dumps(base_result), encoding="ascii")
                    (candidate / name).write_text(json.dumps(candidate_result), encoding="ascii")
        return baseline, candidate

    def test_run_matrix_alternates_package_order(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            baseline_package = root / "baseline-package"
            candidate_package = root / "candidate-package"
            baseline_package.mkdir()
            candidate_package.mkdir()
            baseline_package = baseline_package.resolve()
            candidate_package = candidate_package.resolve()
            output = root / "results"
            args = argparse.Namespace(
                phase="development",
                baseline_package=baseline_package,
                candidate_package=candidate_package,
                output=output,
                scenarios=["S3"],
                overall_max_ratio=0.95,
                scenario_max_ratio=1.03,
            )
            calls = []

            def fake_run_child(command, package_root):
                calls.append(package_root)
                out = Path(command[command.index("-O") + 1])
                seed = int(command[command.index("-s") + 1])
                case = {"index": seed, "elapsed_seconds": 1.0}
                result = {
                    "config": {"seed": seed},
                    "summary": {
                        "total_seconds": 0.9 if package_root == candidate_package else 1.0
                    },
                    "cases": [case],
                }
                out.write_text(json.dumps(result), encoding="ascii")
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

            with mock.patch.object(matrix, "_verify_package"), mock.patch.object(
                matrix, "_run_child", side_effect=fake_run_child
            ):
                comparison = matrix.run_matrix(args)

        expected_order = [
            baseline_package,
            candidate_package,
            candidate_package,
            baseline_package,
            baseline_package,
            candidate_package,
            candidate_package,
            baseline_package,
            baseline_package,
            candidate_package,
        ]
        self.assertEqual(calls[:10], expected_order)
        self.assertEqual(calls[10:], expected_order)
        self.assertTrue(comparison["passed"])

    def test_package_root_rejection(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            package = Path(tmpdir) / "package"
            package.mkdir()
            completed = subprocess.CompletedProcess([], 0, stdout="/outside/fnvcrack/__init__.py\n", stderr="")
            with mock.patch.object(matrix, "_run_child", return_value=completed):
                with self.assertRaisesRegex(matrix.MatrixError, "outside package root"):
                    matrix._verify_package(package)

    def test_compare_rejects_missing_and_extra_pairs(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            baseline, candidate = self.write_results(root, (42,), {"S1": (1.0,)})
            missing = candidate / "S1-seed42-rep5.json"
            missing.unlink()
            with self.assertRaisesRegex(matrix.MatrixError, "missing"):
                matrix.compare_results(baseline, candidate, (42,), ("S1",), 1.0, 1.0)

            missing.write_text((baseline / missing.name).read_text(encoding="ascii"), encoding="ascii")
            (candidate / "extra.json").write_text("{}", encoding="ascii")
            with self.assertRaisesRegex(matrix.MatrixError, "extra"):
                matrix.compare_results(baseline, candidate, (42,), ("S1",), 1.0, 1.0)

    def test_compare_rejects_non_time_case_mismatch(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            baseline, candidate = self.write_results(root, (42,), {"S1": (1.0,)})
            path = candidate / "S1-seed42-rep3.json"
            result = json.loads(path.read_text(encoding="ascii"))
            result["cases"][0]["status"] = 1
            path.write_text(json.dumps(result), encoding="ascii")
            with self.assertRaisesRegex(matrix.MatrixError, "case mismatch"):
                matrix.compare_results(baseline, candidate, (42,), ("S1",), 1.0, 1.0)

    def test_compare_calculates_exact_geometric_means(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            baseline, candidate = self.write_results(
                root,
                (1, 2),
                {"S1": (0.5, 2.0), "S2": (0.25, 0.25)},
            )
            result = matrix.compare_results(
                baseline,
                candidate,
                (1, 2),
                ("S1", "S2"),
                1.0,
                1.0,
            )
            saved = json.loads((root / "comparison.json").read_text(encoding="ascii"))

        self.assertAlmostEqual(result["scenario_ratios"]["S1"], 1.0)
        self.assertAlmostEqual(result["scenario_ratios"]["S2"], 0.25)
        self.assertAlmostEqual(result["overall_ratio"], 0.5)
        self.assertEqual(saved["overall_ratio"], result["overall_ratio"])
        self.assertTrue(result["passed"])

    def test_main_threshold_exit_codes(self):
        passing = {"passed": True}
        failing = {"passed": False}
        argv = [
            "--phase",
            "development",
            "--baseline-package",
            "base",
            "--candidate-package",
            "candidate",
            "--output",
            "out",
        ]
        with mock.patch.object(matrix, "run_matrix", return_value=passing), mock.patch("builtins.print"):
            self.assertEqual(matrix.main(argv), 0)
        with mock.patch.object(matrix, "run_matrix", return_value=failing), mock.patch("builtins.print"):
            self.assertEqual(matrix.main(argv), 1)
        with mock.patch.object(matrix, "run_matrix", side_effect=matrix.MatrixError("bad")), mock.patch("builtins.print"):
            self.assertEqual(matrix.main(argv), 2)

    def test_development_scenario_and_seed_selection(self):
        args = argparse.Namespace(
            phase="development",
            scenarios=["S3", "S8"],
            output=Path("unused"),
        )
        scenarios, seeds, scenario_ids = matrix._phase_inputs(args)
        self.assertIs(scenarios, matrix.DEVELOPMENT_SCENARIOS)
        self.assertEqual(seeds, (42, 1337))
        self.assertEqual(scenario_ids, ("S3", "S8"))
        self.assertIn("-i", scenarios["S8"])
        self.assertNotIn("-I", scenarios["S8"])
        self.assertEqual(scenarios["S12"][scenarios["S12"].index("-m") + 1], "1000")

    def test_holdout_seed_is_created_once_and_reused(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir)
            args = argparse.Namespace(phase="holdout", scenarios=None, output=output)
            with mock.patch.object(matrix.secrets, "randbits", return_value=123456) as randbits:
                first = matrix._phase_inputs(args)
                second = matrix._phase_inputs(args)

            seed_text = (output / "holdout-seed.txt").read_text(encoding="ascii")

        self.assertEqual(first[1], (123456,))
        self.assertEqual(second[1], (123456,))
        self.assertEqual(first[2], tuple(matrix.HOLDOUT_SCENARIOS))
        self.assertEqual(seed_text, "123456\n")
        randbits.assert_called_once_with(32)


if __name__ == "__main__":
    unittest.main()
