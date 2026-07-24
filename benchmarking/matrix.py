#!/usr/bin/env python3
import argparse
import json
import math
import os
import secrets
import statistics
import subprocess
import sys
from pathlib import Path


SCRIPT = Path(__file__).resolve()
ROOT = SCRIPT.parents[1]
BENCH_PATH = SCRIPT.with_name("bench.py")
REPETITIONS = 5
DEVELOPMENT_SEEDS = (42, 1337)
COMMON_DEVELOPMENT_ARGS = ("-b", "4", "-m", "0", "-I", "--warmup", "10", "--detail", "full")
COMMON_HOLDOUT_ARGS = ("--warmup", "10", "--detail", "full")

DEVELOPMENT_SCENARIOS = {
    "S1": COMMON_DEVELOPMENT_ARGS + ("-p", "[a-z]{4}", "-c", "[a-z]", "-n", "50000", "-l", "32", "-o", "0x811c9dc5", "-r", "0x01000193"),
    "S2": COMMON_DEVELOPMENT_ARGS + ("-p", "[a-z]{4}", "-c", "[a-z]", "-n", "50000", "-l", "64"),
    "S3": COMMON_DEVELOPMENT_ARGS + ("-p", "[a-z]{10}", "-c", "[a-z]", "-n", "20", "-l", "64"),
    "S4": COMMON_DEVELOPMENT_ARGS + ("-p", r"[\0-\xff]{8}", "-c", r"[\0-\xff]", "-n", "1000", "-l", "64"),
    "S5": COMMON_DEVELOPMENT_ARGS + ("-p", "[A-Za-z0-9_]{8}", "-c", "[A-Za-z0-9_]", "-n", "10000", "-l", "128", "-o", "0x6c62272e07bb014262b821756295c58d", "-r", "0x1000000000000000000013b"),
    "S6": COMMON_DEVELOPMENT_ARGS + ("-p", "[A-Za-z0-9_]{10}", "-c", "[A-Za-z0-9_]", "-n", "10000", "-l", "128", "-o", "0x6c62272e07bb014262b821756295c58d", "-r", "0x1000000000000000000013b"),
    "S7": COMMON_DEVELOPMENT_ARGS + ("-p", "[A-Za-z0-9_]{10}", "-c", "[A-Za-z0-9_]", "-n", "5000", "-l", "256", "-o", "0xdd268dbcaac550362d98c384c4e576ccc8b1536847b6bbb31023b4c8caee0535", "-r", "0x1000000000000000000000000000000000000000163"),
    "S8": ("-b", "4", "-m", "0", "-i", "--warmup", "10", "--detail", "full", "-p", "[a-z]{8}", "-c", "[a-z]", "-n", "1000", "-l", "64"),
    "S9": ("-b", "4", "-m", "0", "-i", "--warmup", "10", "--detail", "full", "-p", "[A-Za-z0-9_]{8}", "-c", "[A-Za-z0-9_]", "-n", "5000", "-l", "128", "-o", "0x6c62272e07bb014262b821756295c58d", "-r", "0x1000000000000000000013b"),
    "S10": COMMON_DEVELOPMENT_ARGS + ("-p", "[a-z]{8}", "-c", "[a-z]", "-n", "5000", "-l", "64", "-P", "pre_", "-S", "_suf"),
    "S11": COMMON_DEVELOPMENT_ARGS + ("-p", "[a-z]{8}", "-c", "[a-z]", "-n", "10000", "-l", "128", "-o", "0x6c62272e07bb014262b821756295c58d", "-r", "0x1000000000000000000013b", "-P", "pre_", "-S", "_suf"),
    "S12": ("-b", "4", "-m", "1000", "-I", "--warmup", "10", "--detail", "full", "-p", "[a-z]{10}", "-c", "[0-9]", "-n", "5", "-l", "64"),
}

HOLDOUT_SCENARIOS = {
    "H1": COMMON_HOLDOUT_ARGS + ("-p", "[a-z]{8}", "-c", "[a-z]", "-n", "50000", "-l", "64", "-b", "0", "-m", "0", "-I"),
    "H2": COMMON_HOLDOUT_ARGS + ("-p", "[A-Za-z0-9_]{9}", "-c", "[A-Za-z0-9_]", "-n", "500", "-l", "64", "-b", "2", "-m", "10000", "-I"),
    "H3": COMMON_HOLDOUT_ARGS + ("-p", r"[\0-\xff]{9}", "-c", r"[\0-\xff]", "-n", "5000", "-l", "128", "-o", "0x6c62272e07bb014262b821756295c58d", "-r", "0x1000000000000000000013b", "-b", "6", "-m", "0", "-I"),
    "H4": COMMON_HOLDOUT_ARGS + ("-p", "[a-z]{7}", "-c", "[a-z]", "-n", "5000", "-l", "256", "-o", "0xdd268dbcaac550362d98c384c4e576ccc8b1536847b6bbb31023b4c8caee0535", "-r", "0x1000000000000000000000000000000000000000163", "-b", "3", "-m", "0", "-i", "-P", "pre_", "-S", "_suf"),
}


class MatrixError(RuntimeError):
    pass


def _geometric_mean(values):
    return math.exp(sum(math.log(value) for value in values) / len(values))


def _holdout_seed(output):
    path = output / "holdout-seed.txt"
    if path.exists():
        try:
            seed = int(path.read_text(encoding="ascii").strip())
        except ValueError as exc:
            raise MatrixError(f"invalid holdout seed in {path}") from exc
        if not 0 <= seed < 1 << 32:
            raise MatrixError(f"invalid holdout seed in {path}")
        return seed

    seed = secrets.randbits(32)
    path.write_text(f"{seed}\n", encoding="ascii")
    return seed


def _phase_inputs(args):
    if args.phase == "development":
        scenarios = DEVELOPMENT_SCENARIOS
        seeds = DEVELOPMENT_SEEDS
    else:
        scenarios = HOLDOUT_SCENARIOS
        seeds = (_holdout_seed(args.output),)

    scenario_ids = tuple(args.scenarios or scenarios)
    unknown = [scenario_id for scenario_id in scenario_ids if scenario_id not in scenarios]
    if unknown:
        raise MatrixError(f"unknown {args.phase} scenario: {unknown[0]}")
    return scenarios, seeds, scenario_ids


def _package_env(package_root):
    env = os.environ.copy()
    env["PYTHONPATH"] = str(package_root.resolve())
    return env


def _run_child(command, package_root):
    completed = subprocess.run(
        command,
        cwd=ROOT,
        env=_package_env(package_root),
        capture_output=True,
        text=True,
    )
    if completed.returncode:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise MatrixError(f"child failed ({completed.returncode}): {detail}")
    return completed


def _verify_package(package_root):
    package_root = package_root.resolve()
    completed = _run_child(
        [sys.executable, "-c", "import fnvcrack; print(fnvcrack.__file__)"],
        package_root,
    )
    try:
        imported = Path(completed.stdout.strip()).resolve()
        imported.relative_to(package_root)
    except (ValueError, OSError) as exc:
        raise MatrixError(
            f"fnvcrack resolved outside package root {package_root}: {completed.stdout.strip()}"
        ) from exc
    return imported


def _result_names(directory):
    return {path.name for path in directory.glob("*.json")}


def _case_without_elapsed(case):
    return {key: value for key, value in case.items() if key != "elapsed_seconds"}


def _load_pair(baseline_path, candidate_path):
    try:
        baseline = json.loads(baseline_path.read_text(encoding="ascii"))
        candidate = json.loads(candidate_path.read_text(encoding="ascii"))
    except (OSError, json.JSONDecodeError) as exc:
        raise MatrixError(f"invalid result pair {baseline_path.name}: {exc}") from exc

    if baseline.get("config") != candidate.get("config"):
        raise MatrixError(f"config mismatch in {baseline_path.name}")

    baseline_cases = [_case_without_elapsed(case) for case in baseline.get("cases", [])]
    candidate_cases = [_case_without_elapsed(case) for case in candidate.get("cases", [])]
    if baseline_cases != candidate_cases:
        raise MatrixError(f"case mismatch in {baseline_path.name}")
    return baseline, candidate


def _comparison_path(baseline_dir, candidate_dir):
    if baseline_dir.parent == candidate_dir.parent:
        return baseline_dir.parent / "comparison.json"
    return Path("comparison.json")


def compare_results(
    baseline_dir,
    candidate_dir,
    seeds,
    scenario_ids,
    overall_max_ratio,
    scenario_max_ratio,
):
    baseline_dir = Path(baseline_dir).resolve()
    candidate_dir = Path(candidate_dir).resolve()
    expected = {
        f"{scenario_id}-seed{seed}-rep{repetition}.json"
        for scenario_id in scenario_ids
        for seed in seeds
        for repetition in range(1, REPETITIONS + 1)
    }
    baseline_names = _result_names(baseline_dir)
    candidate_names = _result_names(candidate_dir)
    if baseline_names != expected or candidate_names != expected:
        raise MatrixError(
            "result file set mismatch: "
            f"baseline missing={sorted(expected - baseline_names)} extra={sorted(baseline_names - expected)}; "
            f"candidate missing={sorted(expected - candidate_names)} extra={sorted(candidate_names - expected)}"
        )

    per_seed = {}
    scenario_ratios = {}
    for scenario_id in scenario_ids:
        per_seed[scenario_id] = {}
        seed_ratios = []
        for seed in seeds:
            baseline_totals = []
            candidate_totals = []
            for repetition in range(1, REPETITIONS + 1):
                name = f"{scenario_id}-seed{seed}-rep{repetition}.json"
                baseline, candidate = _load_pair(
                    baseline_dir / name,
                    candidate_dir / name,
                )
                baseline_totals.append(baseline["summary"]["total_seconds"])
                candidate_totals.append(candidate["summary"]["total_seconds"])

            baseline_median = statistics.median(baseline_totals)
            candidate_median = statistics.median(candidate_totals)
            if baseline_median <= 0 or candidate_median <= 0:
                raise MatrixError(f"non-positive median for {scenario_id} seed {seed}")
            ratio = candidate_median / baseline_median
            per_seed[scenario_id][str(seed)] = {
                "baseline_median_seconds": baseline_median,
                "candidate_median_seconds": candidate_median,
                "ratio": ratio,
            }
            seed_ratios.append(ratio)

        scenario_ratios[scenario_id] = _geometric_mean(seed_ratios)

    overall_ratio = _geometric_mean(list(scenario_ratios.values()))
    passed = overall_ratio <= overall_max_ratio and all(
        ratio <= scenario_max_ratio for ratio in scenario_ratios.values()
    )
    comparison = {
        "baseline_package": str(baseline_dir),
        "candidate_package": str(candidate_dir),
        "per_seed": per_seed,
        "scenario_ratios": scenario_ratios,
        "overall_ratio": overall_ratio,
        "overall_max_ratio": overall_max_ratio,
        "scenario_max_ratio": scenario_max_ratio,
        "passed": passed,
    }
    _comparison_path(baseline_dir, candidate_dir).write_text(
        json.dumps(comparison, ensure_ascii=True, indent=2, sort_keys=True),
        encoding="ascii",
    )
    return comparison


def run_matrix(args):
    args.output.mkdir(parents=True, exist_ok=True)
    scenarios, seeds, scenario_ids = _phase_inputs(args)
    packages = {
        "baseline": args.baseline_package.resolve(),
        "candidate": args.candidate_package.resolve(),
    }
    for package_root in packages.values():
        _verify_package(package_root)

    result_dirs = {name: args.output / name for name in packages}
    for directory in result_dirs.values():
        directory.mkdir(parents=True, exist_ok=True)

    for scenario_id in scenario_ids:
        for seed in seeds:
            for repetition in range(1, REPETITIONS + 1):
                order = ("baseline", "candidate") if repetition % 2 else ("candidate", "baseline")
                name = f"{scenario_id}-seed{seed}-rep{repetition}.json"
                for package_name in order:
                    command = [
                        sys.executable,
                        str(BENCH_PATH),
                        "perf",
                        *scenarios[scenario_id],
                        "-s",
                        str(seed),
                        "-O",
                        str(result_dirs[package_name] / name),
                    ]
                    _run_child(command, packages[package_name])

    comparison = compare_results(
        result_dirs["baseline"],
        result_dirs["candidate"],
        seeds,
        scenario_ids,
        args.overall_max_ratio,
        args.scenario_max_ratio,
    )
    comparison["baseline_package"] = str(packages["baseline"])
    comparison["candidate_package"] = str(packages["candidate"])
    _comparison_path(result_dirs["baseline"], result_dirs["candidate"]).write_text(
        json.dumps(comparison, ensure_ascii=True, indent=2, sort_keys=True),
        encoding="ascii",
    )
    return comparison


def build_parser():
    parser = argparse.ArgumentParser(description="Compare fnvcrack benchmark packages.")
    parser.add_argument("--phase", choices=("development", "holdout"), required=True)
    parser.add_argument("--baseline-package", type=Path, required=True)
    parser.add_argument("--candidate-package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenarios", nargs="+")
    parser.add_argument("--overall-max-ratio", type=float, default=0.95)
    parser.add_argument("--scenario-max-ratio", type=float, default=1.03)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        comparison = run_matrix(args)
    except MatrixError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    print(json.dumps(comparison, ensure_ascii=True, indent=2, sort_keys=True))
    return 0 if comparison["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
