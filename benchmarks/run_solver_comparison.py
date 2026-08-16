#!/usr/bin/env python3
"""Run kmx-sat, CaDiCaL and Kissat on CNF inputs and print a comparison table."""

from __future__ import annotations

import argparse
import re
import os
import signal
import shutil
import subprocess
import statistics
import time
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOLVER = REPOSITORY_ROOT / "source/build/release/default/kmx-sat.d9e8dc1a/kmx-sat"
DEFAULT_CADICAL = REPOSITORY_ROOT / "tools/bin/cadical"
DEFAULT_KISSAT = REPOSITORY_ROOT / "tools/bin/kissat"
STATUS_PATTERN = re.compile(r"^s (SATISFIABLE|UNSATISFIABLE)$", re.MULTILINE)
SOLVER_STATUS_EXIT_CODES = {0, 10, 20}


def run_solver(executable: Path, instance: Path, timeout: float, cpu: int | None = None) -> tuple[str, float, bool, int]:
    command = [str(executable), str(instance)]
    if cpu is not None:
        command = ["taskset", "-c", str(cpu), *command]
    started = time.perf_counter_ns()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        output, _ = process.communicate(timeout=timeout)
        timed_out = False
        exit_code = process.returncode
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        timed_out = True
        exit_code = -1
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            output += process.communicate(timeout=1.0)[0] or b""
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output += process.communicate()[0] or b""
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
    if isinstance(output, bytes):
        output = output.decode(errors="replace")
    match = STATUS_PATTERN.search(output)
    return (match.group(1) if match else "UNKNOWN"), elapsed_ms, timed_out, exit_code


def summarize(samples: list[float]) -> tuple[float, float, float]:
    if not samples:
        return 0.0, 0.0, 0.0
    ordered = sorted(samples)
    return min(ordered), statistics.median(ordered), ordered[min(len(ordered) - 1, int(len(ordered) * 0.9))]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("instances", nargs="*", type=Path, help="CNF files (default: benchmarks/corpus/*.cnf).")
    parser.add_argument("--timeout", type=float, default=30.0, help="Per-solver timeout in seconds.")
    parser.add_argument("--warmup-count", type=int, default=2, help="Unreported warmup runs per solver and instance.")
    parser.add_argument("--repeat-count", type=int, default=10, help="Reported interleaved runs per solver and instance.")
    parser.add_argument("--cpu", type=int, help="Pin solver processes to one logical CPU with taskset.")
    parser.add_argument("--include-extended", action="store_true", help="Also run benchmarks/corpus/extended/*.cnf.")
    parser.add_argument("--solver", type=Path, default=DEFAULT_SOLVER)
    parser.add_argument("--cadical", type=Path, default=DEFAULT_CADICAL)
    parser.add_argument("--kissat", type=Path, default=DEFAULT_KISSAT)
    args = parser.parse_args()

    if args.warmup_count < 0 or args.repeat_count < 1:
        parser.error("--warmup-count must be non-negative and --repeat-count must be at least 1")
    if args.cpu is not None and shutil.which("taskset") is None:
        parser.error("--cpu requires taskset")

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
        f"{'kissat median':>14}",
        f"{'cadical median':>15}",
        f"{'kmx-sat median':>15}",
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
        samples: dict[str, list[float]] = {name: [] for name, _ in solvers}
        for _ in range(args.warmup_count):
            for name, executable in solvers:
                run_solver(executable, instance, args.timeout, args.cpu)
        for repeat in range(args.repeat_count):
            ordered_solvers = solvers[repeat % len(solvers):] + solvers[:repeat % len(solvers)]
            for name, executable in ordered_solvers:
                status, elapsed_ms, timed_out, solver_exit_code = run_solver(executable, instance, args.timeout, args.cpu)
                if not timed_out and status != "UNKNOWN":
                    samples[name].append(elapsed_ms)
                statuses.setdefault(name, status)
                if status != statuses[name]:
                    statuses[name] = "UNKNOWN"
                if timed_out:
                    timed_out_solvers.add(name)
                if solver_exit_code not in SOLVER_STATUS_EXIT_CODES:
                    statuses[name] = "UNKNOWN"

        summaries = {name: summarize(values) for name, values in samples.items()}
        results = {name: summaries[name][1] for name, _ in solvers}

        unique_statuses = set(statuses.values())
        status_mismatch = len(unique_statuses) > 1 or "UNKNOWN" in unique_statuses
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
        for name, _ in solvers:
            minimum, median, p90 = summaries[name]
            print(f"  {name}: min={minimum:.3f} ms median={median:.3f} ms p90={p90:.3f} ms")

    if any_mismatch:
        print("\n* indicates status mismatch between solvers for that instance.")
    print()
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
