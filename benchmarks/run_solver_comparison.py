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

    header_parts = [
        f"{'Input':<32}",
        f"{'kissat (ms)':>11}",
        f"{'cadical (ms)':>12}",
        f"{'kmx-sat (ms)':>12}",
        f"{'kmx-sat/cadical':>14}",
        f"{'kmx-sat/kissat':>13}",
    ]
    header = " | ".join(header_parts)
    print()
    print(header)
    print("-" * len(header))

    exit_code = 0
    any_mismatch = False
    for instance in instances:
        if not instance.is_file():
            print(f"{instance.name:<32} | {'MISSING':>12} | {'-':>13} | {'-':>13} | {'-':>28} | {'-':>27}")
            exit_code = 1
            continue

        results: dict[str, float] = {}
        statuses: dict[str, str] = {}
        timed_out_solvers: set[str] = set()
        for name, executable in solvers:
            status, elapsed_ms, timed_out = run_solver(executable, instance, args.timeout)
            results[name] = elapsed_ms
            statuses[name] = status
            if timed_out:
                timed_out_solvers.add(name)

        unique_statuses = {s for s in statuses.values() if s != "UNKNOWN"}
        status_mismatch = len(unique_statuses) > 1
        if status_mismatch:
            any_mismatch = True
        instance_name_str = f"{instance.name}{'*' if status_mismatch else ''}"

        row_data: dict[str, str] = {"instance": instance_name_str}
        for name in ("kissat", "cadical", "kmx-sat"):
            if name in timed_out_solvers:
                row_data[name] = "T/O"
            else:
                row_data[name] = f"{results.get(name, 0.0):.3f}"

        kmx_time = results.get("kmx-sat")
        cadical_time = results.get("cadical")
        kissat_time = results.get("kissat")

        vs_cadical_str = "-"
        if "cadical" in timed_out_solvers:
            vs_cadical_str = "T/O"
        elif "kmx-sat" not in timed_out_solvers and kmx_time is not None and cadical_time is not None and cadical_time > 0:
            vs_cadical = ((kmx_time / cadical_time) - 1) * 100
            vs_cadical_str = f"{vs_cadical:+.1f}%"

        vs_kissat_str = "-"
        if "kissat" in timed_out_solvers:
            vs_kissat_str = "T/O"
        elif "kmx-sat" not in timed_out_solvers and kmx_time is not None and kissat_time is not None and kissat_time > 0:
            vs_kissat = ((kmx_time / kissat_time) - 1) * 100
            vs_kissat_str = f"{vs_kissat:+.1f}%"

        print(
            f"{row_data['instance']:<32} | "
            f"{row_data['kissat']:>11} | "
            f"{row_data['cadical']:>12} | "
            f"{row_data['kmx-sat']:>12} | "
            f"{vs_cadical_str:>15} | "
            f"{vs_kissat_str:>14}"
        )

    if any_mismatch:
        print("\n* indicates status mismatch between solvers for that instance.")
    print()
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
