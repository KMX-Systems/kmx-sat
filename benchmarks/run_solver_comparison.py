#!/usr/bin/env python3
"""Run kmx-sat, CaDiCaL and Kissat on CNF inputs and print a comparison table.

Without explicit instances the script runs the three pinned sets under benchmarks/corpus: the base corpus,
`extended/` and the classic SATLIB/DIMACS families in `classic/`; `--set` restricts a run to some of them.
Every instance gets `--warmup-count` unreported rounds followed by `--repeat-count` reported rounds, each
round running the three solvers in rotating order. The classic families range from milliseconds to the
timeout, so an instance stops repeating once its rounds have consumed `--instance-budget` seconds (one
reported round always runs), and a solver that times out on an instance is not run on it again. Statuses
are checked between the solvers and against the `manifest.json` of the set that pins the instance.
"""

from __future__ import annotations

import argparse
import json
import re
import os
import signal
import shutil
import subprocess
import statistics
import time
from dataclasses import dataclass, field
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOLVER = REPOSITORY_ROOT / "source/build/release/default/kmx-sat.d9e8dc1a/kmx-sat"
DEFAULT_CADICAL = REPOSITORY_ROOT / "tools/bin/cadical"
DEFAULT_KISSAT = REPOSITORY_ROOT / "tools/bin/kissat"
STATUS_PATTERN = re.compile(r"^s (SATISFIABLE|UNSATISFIABLE)$", re.MULTILINE)
SOLVER_STATUS_EXIT_CODES = {0, 10, 20}
DECIDED_STATUSES = {"SATISFIABLE", "UNSATISFIABLE"}
INSTANCE_SETS: dict[str, Path] = {
    "corpus": REPOSITORY_ROOT / "benchmarks/corpus",
    "extended": REPOSITORY_ROOT / "benchmarks/corpus/extended",
    "classic": REPOSITORY_ROOT / "benchmarks/corpus/classic",
}
REPORT_ORDER = ("kissat", "cadical", "kmx-sat")
COLUMNS = (
    ("Input", 36, "<"),
    ("kissat median", 14, ">"),
    ("cadical median", 15, ">"),
    ("kmx-sat median", 15, ">"),
    ("kmx-sat/cadical", 15, ">"),
    ("kmx-sat/kissat", 14, ">"),
)


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


def load_expected_statuses() -> dict[Path, str]:
    """Maps every instance pinned by a set manifest to its expected status."""
    expected: dict[Path, str] = {}
    for directory in INSTANCE_SETS.values():
        manifest = directory / "manifest.json"
        if not manifest.is_file():
            continue
        document = json.loads(manifest.read_text(encoding="utf-8"))
        for entry in document.get("instances", []):
            status = entry.get("expected_status")
            if status in DECIDED_STATUSES and "file" in entry:
                expected[(directory / entry["file"]).resolve()] = status
    return expected


@dataclass
class measurement:
    """What the rounds on one instance established about each solver."""

    samples: dict[str, list[float]]
    statuses: dict[str, str] = field(default_factory=dict)
    timed_out: set[str] = field(default_factory=set)
    failed: set[str] = field(default_factory=set)
    reported_rounds: int = 0

    def decided_status(self, name: str) -> str | None:
        status = self.statuses.get(name)
        return status if status in DECIDED_STATUSES else None


def measure_instance(instance: Path, solvers: list[tuple[str, Path]], args: argparse.Namespace) -> measurement:
    """Runs the warmup and reported rounds on one instance under the per-instance budget."""
    result = measurement(samples={name: [] for name, _ in solvers})
    spent_seconds = 0.0
    warmups_remaining = args.warmup_count
    round_index = 0
    while True:
        reporting = warmups_remaining == 0
        shift = round_index % len(solvers)
        round_started = time.perf_counter()
        for name, executable in solvers[shift:] + solvers[:shift]:
            if name in result.timed_out:
                continue
            status, elapsed_ms, timed_out, exit_code = run_solver(executable, instance, args.timeout, args.cpu)
            if timed_out:
                result.timed_out.add(name)
                continue
            if not reporting:
                continue
            if exit_code not in SOLVER_STATUS_EXIT_CODES or status not in DECIDED_STATUSES:
                result.failed.add(name)
                result.statuses[name] = "UNKNOWN"
                continue
            result.samples[name].append(elapsed_ms)
            if result.statuses.setdefault(name, status) != status:
                result.statuses[name] = "UNKNOWN"
                result.failed.add(name)
        spent_seconds += time.perf_counter() - round_started
        round_index += 1
        if reporting:
            result.reported_rounds += 1
            if result.reported_rounds >= args.repeat_count or spent_seconds >= args.instance_budget:
                break
        else:
            warmups_remaining -= 1
            if spent_seconds * 4.0 >= args.instance_budget:
                warmups_remaining = 0
        if len(result.timed_out) == len(solvers):
            break
    return result


def format_row(cells: list[str]) -> str:
    return " | ".join(f"{cell:{align}{width}}" for cell, (_, width, align) in zip(cells, COLUMNS))


def ratio_cell(result: measurement, medians: dict[str, float], reference: str) -> str:
    if reference in result.timed_out:
        return "T/O"
    if "kmx-sat" in result.timed_out or reference in result.failed or "kmx-sat" in result.failed:
        return "-"
    if medians[reference] <= 0.0:
        return "-"
    return f"{(medians['kmx-sat'] / medians[reference] - 1.0) * 100.0:+.1f}%"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("instances", nargs="*", type=Path,
                        help="CNF files to run instead of the pinned sets.")
    parser.add_argument("--set", dest="sets", action="append", choices=sorted(INSTANCE_SETS),
                        help="Pinned set to run (repeatable); default: corpus, extended and classic. "
                             "Ignored when instances are given.")
    parser.add_argument("--timeout", type=float, default=30.0, help="Per-solver timeout in seconds.")
    parser.add_argument("--warmup-count", type=int, default=2, help="Unreported warmup rounds per instance.")
    parser.add_argument("--repeat-count", type=int, default=10, help="Reported interleaved rounds per instance.")
    parser.add_argument("--instance-budget", type=float, default=60.0,
                        help="Seconds an instance may consume across all solvers before its rounds stop; one "
                             "reported round always runs, and warmups are skipped once a single round used a "
                             "quarter of the budget.")
    parser.add_argument("--cpu", type=int, help="Pin solver processes to one logical CPU with taskset.")
    parser.add_argument("--solver", type=Path, default=DEFAULT_SOLVER)
    parser.add_argument("--cadical", type=Path, default=DEFAULT_CADICAL)
    parser.add_argument("--kissat", type=Path, default=DEFAULT_KISSAT)
    args = parser.parse_args()

    if args.warmup_count < 0 or args.repeat_count < 1:
        parser.error("--warmup-count must be non-negative and --repeat-count must be at least 1")
    if args.instance_budget <= 0.0:
        parser.error("--instance-budget must be positive")
    if args.cpu is not None and shutil.which("taskset") is None:
        parser.error("--cpu requires taskset")

    instances = list(args.instances)
    if not instances:
        for name in args.sets or list(INSTANCE_SETS):
            instances += sorted(INSTANCE_SETS[name].glob("*.cnf"))
    if not instances:
        parser.error("no CNF instances found")

    solvers = [("kmx-sat", args.solver), ("cadical", args.cadical), ("kissat", args.kissat)]
    missing = [name for name, executable in solvers if not executable.is_file()]
    if missing:
        parser.error(f"missing solver executable(s): {', '.join(missing)}")

    expected_statuses = load_expected_statuses()
    header = format_row([title for title, _, _ in COLUMNS])
    print()
    print(header)
    print("-" * len(header))

    exit_code = 0
    any_mismatch = False
    any_contradiction = False
    totals = {name: {"solved": 0, "timed_out": 0, "failed": 0, "seconds": 0.0} for name, _ in solvers}
    for instance in instances:
        if not instance.is_file():
            print(format_row([instance.name, "MISSING", "-", "-", "-", "-"]))
            exit_code = 1
            continue

        result = measure_instance(instance, solvers, args)
        medians = {name: summarize(values)[1] for name, values in result.samples.items()}

        decided = {status for name, _ in solvers if (status := result.decided_status(name)) is not None}
        mismatch = len(decided) > 1 or bool(result.failed)
        expected = expected_statuses.get(instance.resolve())
        contradiction = expected is not None and any(status != expected for status in decided)
        any_mismatch |= mismatch
        any_contradiction |= contradiction
        if mismatch or contradiction:
            exit_code = 1

        cells = [f"{instance.name}{'*' if mismatch else ''}{'!' if contradiction else ''}"]
        for name in REPORT_ORDER:
            if name in result.timed_out:
                cells.append("T/O")
                totals[name]["timed_out"] += 1
            elif name in result.failed:
                cells.append("FAIL")
                totals[name]["failed"] += 1
            else:
                cells.append(f"{medians[name]:.3f}")
                totals[name]["solved"] += 1
                totals[name]["seconds"] += medians[name] / 1000.0
        cells.append(ratio_cell(result, medians, "cadical"))
        cells.append(ratio_cell(result, medians, "kissat"))
        print(format_row(cells), flush=True)
        for name, _ in solvers:
            minimum, median, p90 = summarize(result.samples[name])
            print(f"  {name}: min={minimum:.3f} ms median={median:.3f} ms p90={p90:.3f} ms "
                  f"({len(result.samples[name])} of {result.reported_rounds} reported rounds)")

    print()
    print(f"Totals over {len(instances)} instances (sum of per-instance medians, solved instances only):")
    for name in REPORT_ORDER:
        counts = totals[name]
        print(f"  {name:<8} {counts['seconds']:9.3f} s  solved {counts['solved']:>3}  "
              f"timed out {counts['timed_out']:>3}  failed {counts['failed']:>3}")
    if any_mismatch:
        print("\n* the solvers disagree on the status, or one of them failed.")
    if any_contradiction:
        print("\n! a reported status contradicts the expected status pinned in the set's manifest.")
    print()
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
