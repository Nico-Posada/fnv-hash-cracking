#!/usr/bin/env python3
import argparse
import codecs
import json
import platform
import random
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import warnings
from collections import Counter
from pathlib import Path

import exrex

from fnvcrack import (
    CrackContext,
    FNV64_OFFSET_BASIS,
    FNV64_PRIME,
    __version__ as FNVCRACK_VERSION,
)


SCRIPT = Path(__file__).resolve()
ROOT = SCRIPT.parents[1]
MAX_VALID_CHAR_GENERATION = 300
PY_SPY_RATE = 100
PROFILE_LIMIT = 20


class BenchError(ValueError):
    pass


def decode_escapes(value):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        try:
            return codecs.escape_decode(value.encode("latin-1"))[0].decode("latin-1")
        except (UnicodeEncodeError, ValueError) as exc:
            raise BenchError(str(exc)) from exc


def text_to_bytes(value, name):
    try:
        return value.encode("latin-1")
    except UnicodeEncodeError as exc:
        raise BenchError(f"{name} contains a character above \\xff") from exc


def decode_bytes(value, name):
    return text_to_bytes(decode_escapes(value), name)


def fnv1a(data, offset_basis, prime, bit_length):
    mask = (1 << bit_length) - 1
    hsh = offset_basis
    for byte in data:
        hsh ^= byte
        hsh *= prime
        hsh &= mask
    return hsh


def valid_chars_from_pattern(pattern):
    decoded = decode_escapes(pattern)
    chars = set()

    for match in exrex.generate(decoded, limit=MAX_VALID_CHAR_GENERATION):
        if len(match) != 1:
            raise BenchError("--valid-chars-pattern must only match single bytes")
        chars.add(text_to_bytes(match, "--valid-chars-pattern")[0])

    if not chars:
        raise BenchError("--valid-chars-pattern produced no bytes")

    return bytes(sorted(chars))


def generate_cases(args, count=None):
    pattern = decode_escapes(args.pattern)
    random.seed(args.seed)

    for index in range(args.count if count is None else count):
        unknown = text_to_bytes(exrex.getone(pattern), "--pattern")
        plaintext = args.prefix + unknown + args.suffix
        target = fnv1a(plaintext, args.offset_basis, args.prime, args.bit_length)
        yield {
            "index": index,
            "crack_len": len(unknown),
            "unknown_hex": unknown.hex(),
            "plaintext_hex": plaintext.hex(),
            "target_hash": target,
        }


def percentile(values, pct):
    ordered = sorted(values)
    index = int((len(ordered) - 1) * pct / 100)
    return ordered[index]


def summarize(rows):
    times = [row["elapsed_seconds"] for row in rows]
    successes = sum(1 for row in rows if row["success"])
    total = sum(times)
    return {
        "count": len(rows),
        "success": successes,
        "failure": len(rows) - successes,
        "collision": sum(
            1 for row in rows if row["success"] and not row["matched_original"]
        ),
        "total_seconds": total,
        "mean_seconds": statistics.mean(times),
        "median_seconds": statistics.median(times),
        "max_seconds": max(times),
        "p95_seconds": percentile(times, 95),
        "throughput_cases_per_second": len(rows) / total if total else 0,
    }


def environment():
    return {
        "fnvcrack_version": FNVCRACK_VERSION,
        "platform": platform.platform(),
        "python": platform.python_version(),
        "python_executable": sys.executable,
    }


def base_config(args, valid_chars):
    return {
        "pattern": args.pattern,
        "valid_chars_pattern": args.valid_chars_pattern,
        "valid_chars_count": len(valid_chars),
        "valid_chars_hex": valid_chars.hex(),
        "count": args.count,
        "warmup": args.warmup,
        "seed": args.seed,
        "incremental": args.incremental,
        "enum_bound": args.enum_bound,
        "max_enum_candidates": args.max_enum_candidates,
        "offset_basis": args.offset_basis,
        "prime": args.prime,
        "bit_length": args.bit_length,
        "prefix_hex": args.prefix.hex(),
        "suffix_hex": args.suffix.hex(),
    }


def _crack_case(ctx, case, args):
    return ctx.crack(
        case["target_hash"],
        crack_len=case["crack_len"],
        enum_bound=args.enum_bound,
        max_enum_candidates=args.max_enum_candidates,
        incremental=args.incremental,
    )


def run_perf(args):
    valid_chars = valid_chars_from_pattern(args.valid_chars_pattern)
    ctx = CrackContext(
        offset_basis=args.offset_basis,
        prime=args.prime,
        bit_length=args.bit_length,
        valid_chars=valid_chars,
        prefix=args.prefix,
        suffix=args.suffix,
    )

    cases = generate_cases(args, args.warmup + args.count)
    for _ in range(args.warmup):
        _crack_case(ctx, next(cases), args)

    rows = []
    for case in cases:
        start = time.perf_counter_ns()
        result = _crack_case(ctx, case, args)
        elapsed_ns = time.perf_counter_ns() - start

        result_hex = result.value.hex() if result.value is not None else None
        cracked_hash = (
            fnv1a(result.value, args.offset_basis, args.prime, args.bit_length)
            if result.value is not None
            else None
        )
        success = cracked_hash == case["target_hash"]
        matched_original = success and result_hex == case["plaintext_hex"]
        rows.append(
            {
                **case,
                "elapsed_seconds": elapsed_ns / 1_000_000_000,
                "status": int(result.status),
                "status_name": result.status_name,
                "success": success,
                "matched_original": matched_original,
                "result_hex": result_hex,
            }
        )

    slowest = sorted(rows, key=lambda row: row["elapsed_seconds"], reverse=True)[:10]
    return {
        "mode": "perf",
        "config": base_config(args, valid_chars),
        "environment": environment(),
        "summary": summarize(rows),
        "slowest": slowest,
        "cases": rows,
    }


def child_perf_args(args):
    child = [
        "perf",
        "-p",
        args.pattern,
        "-c",
        args.valid_chars_pattern,
        "-n",
        str(args.count),
        "--warmup",
        str(args.warmup),
        "-b",
        str(args.enum_bound),
        "-m",
        str(args.max_enum_candidates),
        "-o",
        hex(args.offset_basis),
        "-r",
        hex(args.prime),
        "-l",
        str(args.bit_length),
        "-s",
        str(args.seed),
        "-P",
        args.prefix_raw,
        "-S",
        args.suffix_raw,
        "-j",
        "--detail",
        args.detail,
    ]
    child.append("-i" if args.incremental else "-I")
    return child


def frame_location(frame):
    file = frame.get("file")
    line = frame.get("line")
    if file and line:
        return f"{file}:{line}"
    return file


def frame_summary(frame):
    location = frame_location(frame)
    kind = "native"
    if location and ".py" in location:
        kind = "python"
    elif not location or location == "?":
        kind = "unknown"

    return {
        "name": frame.get("name"),
        "location": location,
        "kind": kind,
    }


def frame_info(frame, seconds, total_seconds):
    return {
        **frame_summary(frame),
        "seconds": seconds,
        "percent": seconds * 100 / total_seconds if total_seconds else 0,
    }


def parse_speedscope_profile(path):
    profile = json.loads(path.read_text(encoding="utf-8"))
    frames = profile["shared"]["frames"]
    self_seconds = Counter()
    inclusive_seconds = Counter()
    sample_count = 0
    total_seconds = 0

    for thread in profile.get("profiles", []):
        samples = thread.get("samples", [])
        weights = thread.get("weights") or [1] * len(samples)
        sample_count += len(samples)
        total_seconds += sum(weights)

        for stack, seconds in zip(samples, weights):
            if not stack:
                continue
            stack = tuple(stack)
            self_seconds[stack[-1]] += seconds
            inclusive_seconds.update({frame: seconds for frame in set(stack)})

    def top(counter):
        return [
            frame_info(frames[frame], seconds, total_seconds)
            for frame, seconds in counter.most_common(PROFILE_LIMIT)
        ]

    return {
        "sample_rate_hz": PY_SPY_RATE,
        "sample_count": sample_count,
        "sampled_seconds": total_seconds,
        "top_self_functions": top(self_seconds),
        "top_inclusive_functions": top(inclusive_seconds),
    }


def extract_json(stdout):
    text = stdout.strip()
    start = text.find("{")
    end = text.rfind("}") + 1
    if start < 0 or end <= start:
        raise BenchError("py-spy child did not produce JSON benchmark output")
    return json.loads(text[start:end])


def py_spy_path():
    py_spy = shutil.which("py-spy")
    if not py_spy:
        raise BenchError(
            "py-spy is required. Run with uv run --group benchmark ./benchmarking/bench.py ..."
        )
    return py_spy


def run_py_spy(args, output, output_format):
    cmd = [
        py_spy_path(),
        "record",
        "--native",
        "--function",
        "--full-filenames",
        "--rate",
        str(PY_SPY_RATE),
        "--format",
        output_format,
        "--output",
        str(output),
        "--",
        sys.executable,
        str(SCRIPT),
        *child_perf_args(args),
    ]
    return subprocess.run(
        cmd,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def run_speedscope(args):
    with tempfile.TemporaryDirectory() as tmpdir:
        speedscope_out = args.speedscope_out or Path(tmpdir) / "profile.speedscope.json"
        if args.speedscope_out:
            args.speedscope_out.parent.mkdir(parents=True, exist_ok=True)

        speedscope_proc = run_py_spy(args, speedscope_out, "speedscope")
        if speedscope_proc.returncode:
            sys.stderr.write(speedscope_proc.stderr)
            raise SystemExit(speedscope_proc.returncode)

        result = extract_json(speedscope_proc.stdout)
        result["mode"] = "speedscope"
        result["profiler"] = parse_speedscope_profile(speedscope_out)
        if args.speedscope_out:
            result["profiler"]["speedscope_out"] = str(args.speedscope_out)
        if args.svg_out:
            result["profiler"]["svg_out"] = str(args.svg_out)

    if args.svg_out:
        args.svg_out.parent.mkdir(parents=True, exist_ok=True)
        svg_proc = run_py_spy(args, args.svg_out, "flamegraph")
        if svg_proc.returncode:
            result["profiler"]["svg_error"] = svg_proc.stderr.strip()

    return result


def report(result, detail):
    if result["mode"] == "suite":
        return result

    if detail == "full":
        return result

    output = {
        "mode": result["mode"],
        "config": result["config"],
        "environment": result["environment"],
        "summary": result["summary"],
        "slowest": result["slowest"],
    }
    if "profiler" in result:
        output["profiler"] = result["profiler"]
    return output


def result_paths(paths):
    seen = set()
    for path in paths:
        candidates = sorted(path.rglob("*.json")) if path.is_dir() else [path]
        for candidate in candidates:
            resolved = candidate.resolve()
            if resolved not in seen:
                seen.add(resolved)
                yield candidate


def load_result(path):
    try:
        result = json.loads(path.read_text(encoding="ascii"))
    except OSError as exc:
        raise BenchError(f"cannot read {path}: {exc}") from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BenchError(f"cannot parse {path}: {exc}") from exc

    return result


def case_times(result):
    cases = result.get("cases")
    if cases is None or len(cases) != result["summary"]["count"]:
        return None
    return [row["elapsed_seconds"] for row in cases]


def run_suite(args):
    runs = []
    skipped = []
    exact_times = []
    exact_percentiles = True

    for path in result_paths(args.paths):
        result = load_result(path)
        if result.get("mode") == "suite":
            skipped.append({"path": str(path), "reason": "suite result"})
            continue
        if result.get("mode") not in ("perf", "speedscope"):
            raise BenchError(f"{path} is not a perf or speedscope result")
        for key in ("config", "summary"):
            if key not in result:
                raise BenchError(f"{path} is missing {key}")

        runs.append(
            {
                "name": path.stem,
                "path": str(path),
                "mode": result["mode"],
                "config": result["config"],
                "summary": result["summary"],
                "ok": result["summary"]["failure"] == 0,
            }
        )

        times = case_times(result)
        if times is None:
            exact_percentiles = False
        else:
            exact_times.extend(times)

    if not runs:
        raise BenchError("no result JSON files found")

    summaries = [row["summary"] for row in runs]
    count = sum(summary["count"] for summary in summaries)
    total = sum(summary["total_seconds"] for summary in summaries)
    if exact_percentiles:
        median_seconds = statistics.median(exact_times)
        p95_seconds = percentile(exact_times, 95)
        percentiles_source = "cases"
    else:
        median_seconds = None
        p95_seconds = None
        percentiles_source = "unavailable"

    return {
        "mode": "suite",
        "summary": {
            "runs": len(runs),
            "count": count,
            "success": sum(summary["success"] for summary in summaries),
            "failure": sum(summary["failure"] for summary in summaries),
            "collision": sum(summary["collision"] for summary in summaries),
            "total_seconds": total,
            "mean_seconds": total / count if count else 0,
            "median_seconds": median_seconds,
            "p95_seconds": p95_seconds,
            "max_seconds": max(summary["max_seconds"] for summary in summaries),
            "throughput_cases_per_second": count / total if total else 0,
            "percentiles_source": percentiles_source,
            "median_of_run_medians_seconds": statistics.median(
                summary["median_seconds"] for summary in summaries
            ),
            "max_run_p95_seconds": max(
                summary["p95_seconds"] for summary in summaries
            ),
        },
        "runs": runs,
        "skipped": skipped,
    }


def print_suite(result):
    summary = result["summary"]
    print("mode: suite")
    print(f"runs: {summary['runs']}")
    print(
        "results: "
        f"{summary['success']}/{summary['count']} ok, "
        f"{summary['failure']} failed, "
        f"{summary['collision']} collisions"
    )
    print(
        "seconds: "
        f"total={summary['total_seconds']:.6f} "
        f"mean={summary['mean_seconds']:.6f} "
        f"max={summary['max_seconds']:.6f}"
    )
    if summary["percentiles_source"] == "cases":
        print(
            "percentiles: "
            f"median={summary['median_seconds']:.6f} "
            f"p95={summary['p95_seconds']:.6f}"
        )
    else:
        print("percentiles: unavailable without full case detail")
        print(
            "run percentiles: "
            f"median_of_medians={summary['median_of_run_medians_seconds']:.6f} "
            f"max_p95={summary['max_run_p95_seconds']:.6f}"
        )
    print(f"throughput: {summary['throughput_cases_per_second']:.3f} cases/s")
    if result["skipped"]:
        print(f"skipped: {len(result['skipped'])} suite result files")
    print("runs:")
    for row in result["runs"]:
        config = row["config"]
        run_summary = row["summary"]
        state = "ok" if row["ok"] else "failed"
        print(
            f"  {row['name']}: {state} seed={config['seed']} "
            f"pattern={config['pattern']} count={run_summary['count']} "
            f"total={run_summary['total_seconds']:.6f}s "
            f"mean={run_summary['mean_seconds']:.6f}s "
            f"median={run_summary['median_seconds']:.6f}s "
            f"p95={run_summary['p95_seconds']:.6f}s "
            f"max={run_summary['max_seconds']:.6f}s "
            f"rate={run_summary['throughput_cases_per_second']:.3f}/s"
        )


def print_human(result):
    if result["mode"] == "suite":
        print_suite(result)
        return

    summary = result["summary"]
    config = result["config"]
    profiler = result.get("profiler")
    print(f"mode: {result['mode']}")
    print(f"pattern: {config['pattern']}")
    print(f"valid_chars: {config['valid_chars_count']}")
    print(f"seed: {config['seed']}")
    print(
        "results: "
        f"{summary['success']}/{summary['count']} ok, "
        f"{summary['failure']} failed, "
        f"{summary['collision']} collisions"
    )
    print(
        "seconds: "
        f"total={summary['total_seconds']:.6f} "
        f"mean={summary['mean_seconds']:.6f} "
        f"median={summary['median_seconds']:.6f} "
        f"p95={summary['p95_seconds']:.6f} "
        f"max={summary['max_seconds']:.6f}"
    )
    print(f"throughput: {summary['throughput_cases_per_second']:.3f} cases/s")
    print("slowest:")
    for row in result["slowest"]:
        print(
            f"  {row['index']}: {row['elapsed_seconds']:.6f}s "
            f"{row['status_name']} len={row['crack_len']} hash={hex(row['target_hash'])}"
        )
    if profiler and profiler.get("top_self_functions"):
        print("profiler top self:")
        for row in profiler["top_self_functions"][:5]:
            print(
                f"  {row['percent']:>5.1f}% {row['seconds']:.6f}s "
                f"{row['name']} [{row['location']}]"
            )
        print("profiler top inclusive:")
        for row in profiler["top_inclusive_functions"][:5]:
            print(
                f"  {row['percent']:>5.1f}% {row['seconds']:.6f}s "
                f"{row['name']} [{row['location']}]"
            )


def emit_result(result, args):
    output = report(result, getattr(args, "detail", "summary"))
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(
            json.dumps(output, ensure_ascii=True, indent=2, sort_keys=True),
            encoding="ascii",
        )

    if args.json:
        print(json.dumps(output, ensure_ascii=True, indent=2, sort_keys=True))
    else:
        print_human(result)


def add_common_args(parser):
    parser.add_argument("-p", "--pattern", required=True)
    parser.add_argument("-c", "--valid-chars-pattern", required=True)
    parser.add_argument("-n", "--count", type=int, required=True)
    parser.add_argument("--warmup", type=int, default=0)
    parser.add_argument("-b", "--enum-bound", type=int, required=True)
    parser.add_argument("-m", "--max-enum-candidates", type=int, required=True)
    parser.add_argument(
        "-o",
        "--offset-basis",
        type=lambda value: int(value, 0),
        default=FNV64_OFFSET_BASIS,
    )
    parser.add_argument(
        "-r",
        "--prime",
        type=lambda value: int(value, 0),
        default=FNV64_PRIME,
    )
    parser.add_argument("-l", "--bit-length", type=int, default=64)
    parser.add_argument("-s", "--seed", type=int)
    parser.add_argument("-P", "--prefix", dest="prefix_raw", default="")
    parser.add_argument("-S", "--suffix", dest="suffix_raw", default="")
    parser.add_argument("-O", "--out", type=Path)
    parser.add_argument("-j", "--json", action="store_true")
    parser.add_argument("--detail", choices=("summary", "full"), default="summary")

    incremental = parser.add_mutually_exclusive_group(required=True)
    incremental.add_argument("-i", "--incremental", dest="incremental", action="store_true")
    incremental.add_argument(
        "-I",
        "--no-incremental",
        dest="incremental",
        action="store_false",
    )


def build_parser():
    parser = argparse.ArgumentParser(description="Benchmark fnvcrack.")
    subparsers = parser.add_subparsers(dest="mode", required=True)

    perf = subparsers.add_parser("perf", description="Run benchmark timings.")
    add_common_args(perf)

    speedscope = subparsers.add_parser(
        "speedscope",
        description="Run benchmark timings with py-spy speedscope output.",
    )
    add_common_args(speedscope)
    speedscope.add_argument("--speedscope-out", type=Path)
    speedscope.add_argument("-g", "--svg-out", type=Path)

    suite = subparsers.add_parser(
        "suite",
        description="Summarize saved benchmark JSON results.",
    )
    suite.add_argument("paths", nargs="+", type=Path)
    suite.add_argument("-O", "--out", type=Path)
    suite.add_argument("-j", "--json", action="store_true")

    return parser


def parse_args(argv):
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.mode == "suite":
        return args

    if args.count <= 0:
        parser.error("--count must be positive")
    if args.warmup < 0:
        parser.error("--warmup cannot be negative")
    if args.enum_bound < 0:
        parser.error("--enum-bound cannot be negative")
    if args.max_enum_candidates < 0:
        parser.error("--max-enum-candidates cannot be negative")
    if args.bit_length <= 0:
        parser.error("--bit-length must be positive")
    if args.offset_basis < 0:
        parser.error("--offset-basis cannot be negative")
    if args.prime < 0:
        parser.error("--prime cannot be negative")

    args.seed = random.randrange(1 << 32) if args.seed is None else args.seed
    try:
        args.prefix = decode_bytes(args.prefix_raw, "--prefix")
        args.suffix = decode_bytes(args.suffix_raw, "--suffix")
    except BenchError as exc:
        parser.error(str(exc))

    return args


def run(args):
    if args.mode == "suite":
        return run_suite(args)
    if args.mode == "perf":
        return run_perf(args)
    return run_speedscope(args)


def main(argv=None):
    parser = build_parser()
    try:
        args = parse_args(sys.argv[1:] if argv is None else argv)
        emit_result(run(args), args)
    except BenchError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
