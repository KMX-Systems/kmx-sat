#!/usr/bin/env python3
"""Run kmx-sat, CaDiCaL and Kissat on CNF inputs and print a comparison table."""

from __future__ import annotations

import argparse
import re
import subprocess
import time
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOLVER = REPOSITORY_ROOT / "source/build/release/default/kmx-sat.d9e8dc1a/kmx-sat"
DEFAULT_CADICAL = REPOSITORY_ROOT / "tools/bin/cadical"
DEFAULT_KISSAT = REPOSITORY_ROOT / "tools/bin/kissat"
STATUS_PATTERN = re.compile(r"^s (SATISFIABLE|UNSATISFIABLE)$", re.MULTILINE)


def run_solver(executable: Path, instance: Path, timeout: float) -> tuple[str, float, bool]:
    started = time.perf_counter_ns()
    try:
        completed = subprocess.run(
            [str(executable), str(instance)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
        output = completed.stdout
        timed_out = False
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        timed_out = True
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
    match = STATUS_PATTERN.search(output)
    return (match.group(1) if match else "UNKNOWN"), elapsed_ms, timed_out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("instances", nargs="*", type=Path, help="CNF files (default: benchmarks/corpus/*.cnf).")
    parser.add_argument("--timeout", type=float, default=30.0, help="Per-solver timeout in seconds.")
    parser.add_argument("--include-extended", action="store_true", help="Also run benchmarks/corpus/extended/*.cnf.")
    parser.add_argument("--solver", type=Path, default=DEFAULT_SOLVER)
    parser.add_argument("--cadical", type=Path, default=DEFAULT_CADICAL)
    parser.add_argument("--kissat", type=Path, default=DEFAULT_KISSAT)
    args = parser.parse_args()

    instances = list(args.instances)
    if not instances:
        instances = sorted((REPOSITORY_ROOT / "benchmarks/corpus").glob("*.cnf"))
        if args.include_extended:
            instances += sorted((REPOSITORY_ROOT / "benchmarks/corpus/extended").glob("*.cnf"))
    if not instances:
        parser.error("no CNF instances found")

    solvers = [("kmx-sat", args.solver), ("cadical", args.cadical), ("kissat", args.kissat)]
    missing = [name for name, executable in solvers if not executable.is_file()]
    if missing:
        parser.error(f"missing solver executable(s): {', '.join(missing)}")

    header = f"{'Input':<32} {'Solver':<10} {'Status':<14} {'Time (ms)':>12} {'Timed Out':>10}"
    print()
    print(header)
    print("-" * len(header))
    exit_code = 0
    for instance in instances:
        if not instance.is_file():
            print(f"{instance.name:<32} {'-':<10} {'MISSING':<14} {'-':>12} {'-':>10}")
            exit_code = 1
            continue
        for name, executable in solvers:
            status, elapsed_ms, timed_out = run_solver(executable, instance, args.timeout)
            print(f"{instance.name:<32} {name:<10} {status:<14} {elapsed_ms:>12.3f} {'yes' if timed_out else 'no':>10}")
        print("-" * len(header))
    print()
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
