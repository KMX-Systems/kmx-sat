#!/usr/bin/env python3
"""Run repository-side benchmark, differential, and KPI release gates."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def run_gate(label: str, command: list[str]) -> None:
    print(f"[gate] {label}")
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if completed.returncode != 0:
        raise SystemExit(f"gate failed: {label} (exit {completed.returncode})")


def test_binaries(build_root: Path) -> list[Path]:
    products = ("cdcl", "proof", "simplify", "runtime", "io", "lib", "types", "telemetry")
    binaries: list[Path] = []
    for product in products:
        matches = sorted(build_root.glob(f"kmx-sat-{product}-test.*/kmx-sat-{product}-test"))
        if not matches:
            raise SystemExit(f"missing split test product: kmx-sat-{product}-test")
        binary = matches[0]
        if not binary.is_file() or not binary.stat().st_mode & 0o111:
            raise SystemExit(f"split test product is not executable: {binary}")
        binaries.append(binary)
    return binaries


def find_solver(build_root: Path) -> Path:
    matches = sorted(build_root.glob("kmx-sat.*/kmx-sat"))
    if not matches:
        raise SystemExit(f"missing solver executable under build root: {build_root}")
    return matches[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, help="Path to an existing kmx-sat executable.")
    parser.add_argument("--output-dir", type=Path, default=Path("benchmark-gate-results"))
    parser.add_argument("--seed", type=int, default=20260813)
    parser.add_argument("--repeat-count", type=int, default=2)
    parser.add_argument("--differential-cases", type=int, default=128)
    parser.add_argument("--build-root", type=Path, default=Path("source/build/default"))
    parser.add_argument("--build", action="store_true", help="Build a fresh Qbs tree before running gates.")
    parser.add_argument("--build-dir", type=Path, default=Path("source/build/release-gate"))
    parser.add_argument("--build-variant", default="debug", choices=("debug", "release"))
    parser.add_argument("--direct-ubsan", action="store_true", help="Build and gate a direct UBSan CLI executable.")
    parser.add_argument("--cadical-command", help="Optional CaDiCaL command template using {instance}.")
    parser.add_argument("--kissat-command", help="Optional Kissat command template using {instance}.")
    args = parser.parse_args()
    if args.repeat_count < 1 or args.differential_cases < 1:
        parser.error("repeat count and differential case count must be positive")
    if args.build:
        build_dir = (ROOT / args.build_dir).resolve() if not args.build_dir.is_absolute() else args.build_dir.resolve()
        run_gate(
            "Qbs build",
            ["qbs", "build", "-f", "source/source.qbs", "-d", str(build_dir), f"qbs.buildVariant:{args.build_variant}"],
        )
        build_root = build_dir / "default"
        solver_path = find_solver(build_root).resolve()
    else:
        if args.solver is None:
            parser.error("--solver is required unless --build is specified")
        if not args.solver.is_file() or not args.solver.stat().st_mode & 0o111:
            parser.error(f"solver is not an executable file: {args.solver}")
        build_root = args.build_root.resolve()
        solver_path = args.solver.resolve()
    try:
        solver_path.relative_to(build_root)
    except ValueError:
        parser.error(f"solver is outside the selected build root: {solver_path}")

    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    corpus_manifest = ROOT / "benchmarks/corpus/manifest.json"
    corpus_glob = sorted((ROOT / "benchmarks/corpus").glob("*.cnf"))
    solver_command = (
        str(solver_path)
        + " {instance} --restart-interval 1 --decision-restart-interval 1"
        + " --reduction-interval 1 --reduction-fraction-percent 100"
    )
    comparison_commands = []
    if args.cadical_command:
        comparison_commands.extend(["--compare-command", f"cadical={args.cadical_command}"])
    if args.kissat_command:
        comparison_commands.extend(["--compare-command", f"kissat={args.kissat_command}"])
    comparison_agreement_option = ["--require-comparison-agreement"] if comparison_commands else []
    benchmark_result = output_dir / "pinned-corpus.json"
    differential_result = output_dir / "differential.json"
    split_tests = test_binaries(build_root)
    ubsan_solver: Path | None = None
    if args.direct_ubsan:
        ubsan_solver = Path(tempfile.mkdtemp(prefix="kmx-sat-ubsan-")) / "kmx-sat-ubsan"
        run_gate(
            "direct UBSan CLI build",
            [
                "g++", "-std=c++26", "-O0", "-g", "-fsanitize=undefined", "-fno-omit-frame-pointer",
                "-I", "source/library/api", "-I", "source/library/inc", "-I", "/usr/local/include",
                "source/cli/kmx-sat-main.cpp", "source/library/src/kmx/sat/solver.cpp",
                "source/library/src/kmx/sat/c_api_adapter.cpp", "-o", str(ubsan_solver),
            ],
        )

    run_gate("corpus manifest", [sys.executable, "benchmarks/validate_corpus.py", str(corpus_manifest)])
    run_gate(
        "pinned benchmark corpus",
        [
            sys.executable,
            "benchmarks/run_benchmarks.py",
            *(str(path) for path in corpus_glob),
            "--manifest",
            str(corpus_manifest),
            "--command",
            solver_command,
            *comparison_agreement_option,
            *comparison_commands,
            "--repeat-count",
            str(args.repeat_count),
            "--seed",
            str(args.seed),
            "--configuration",
            "release-gate",
            "--output",
            str(benchmark_result),
        ],
    )
    run_gate(
        "KPI baseline",
        [sys.executable, "benchmarks/validate_results.py", str(benchmark_result), "--baseline", str(corpus_manifest.parent / "baseline.json")],
    )
    run_gate(
        "randomized differential replay",
        [
            sys.executable,
            "benchmarks/differential_campaign.py",
            "--command",
            solver_command + " {assumptions} --decision-limit {decision_limit} --conflict-limit {conflict_limit}",
            "--seed",
            str(args.seed),
            "--cases",
            str(args.differential_cases),
            "--output",
            str(differential_result),
        ],
    )
    replay_files = sorted(differential_result.parent.joinpath("replays").glob("case-*.jsonl"))
    if not replay_files:
        raise SystemExit("differential replay produced no persisted trace artifacts")
    run_gate(
        "replay artifact validation",
        [sys.executable, "benchmarks/validate_replay_traces.py", *(str(path) for path in replay_files)],
    )
    run_gate(
        "standalone replay execution",
        [
            sys.executable,
            "benchmarks/replay_trace.py",
            *(str(path) for path in replay_files),
            "--command",
            solver_command + " {assumptions} --decision-limit {decision_limit} --conflict-limit {conflict_limit}",
        ],
    )
    for binary in split_tests:
        run_gate(binary.name, [str(binary.resolve())])
    if ubsan_solver is not None:
        run_gate(
            "direct UBSan pinned corpus",
            [
                sys.executable, "benchmarks/run_benchmarks.py", *(str(path) for path in corpus_glob),
                "--manifest", str(corpus_manifest), "--command", str(ubsan_solver) + " {instance}",
                "--repeat-count", str(args.repeat_count), "--seed", str(args.seed),
                "--configuration", "direct-ubsan", "--output", str(output_dir / "ubsan-pinned-corpus.json"),
            ],
        )
        run_gate(
            "direct UBSan differential replay",
            [
                sys.executable, "benchmarks/differential_campaign.py",
                "--command", str(ubsan_solver) + " {instance} {assumptions} --decision-limit {decision_limit} --conflict-limit {conflict_limit}",
                "--seed", str(args.seed), "--cases", str(args.differential_cases),
                "--output", str(output_dir / "ubsan-differential.json"),
            ],
        )

    summary = {
        "solver": str(solver_path),
        "build_root": str(build_root),
        "seed": args.seed,
        "repeat_count": args.repeat_count,
        "differential_cases": args.differential_cases,
        "benchmark_result": str(benchmark_result),
        "differential_result": str(differential_result),
        "split_test_products": [binary.name for binary in split_tests],
        "status": "passed",
        "direct_ubsan": args.direct_ubsan,
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
