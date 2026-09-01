#!/usr/bin/env python3
"""Replay persisted incremental JSON Lines traces through a command-driven solver."""

from __future__ import annotations

import argparse
import itertools
import json
import shlex
import subprocess
import tempfile
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from reference_oracle import (  # noqa: E402
    brute_force_model as enumerate_model,
    find_reference_solver,
    reference_model,
)

from validate_replay_traces import validate


def solver_status(command: str, instance: Path, assumptions: list[int], decision_limit: int, conflict_limit: int) -> str:
    assumption_options = " ".join(f"--assume {literal}" for literal in assumptions)
    completed = subprocess.run(
        command.format(
            instance=shlex.quote(str(instance)),
            assumptions=assumption_options,
            decision_limit=decision_limit,
            conflict_limit=conflict_limit,
        ),
        shell=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if "s SATISFIABLE" in completed.stdout:
        return "SATISFIABLE"
    if "s UNSATISFIABLE" in completed.stdout:
        return "UNSATISFIABLE"
    return "UNKNOWN"


def satisfies_clause(clause: list[int], assignment: int) -> bool:
    for literal in clause:
        bit = (assignment >> (abs(literal) - 1)) & 1
        if (literal > 0 and bit == 1) or (literal < 0 and bit == 0):
            return True
    return False


def evaluate_assignment(clauses: list[list[int]], assumptions: list[int], variable_count: int, assignment: int) -> bool:
    for clause in clauses:
        if not satisfies_clause(clause, assignment):
            return False
    for literal in assumptions:
        bit = (assignment >> (abs(literal) - 1)) & 1
        if (literal > 0 and bit == 0) or (literal < 0 and bit == 1):
            return False
    return True


_REFERENCE_SOLVER = find_reference_solver()


def brute_force_model(clauses: list[list[int]], assumptions: list[int], variable_count: int) -> dict[int, bool] | None:
    """Find a satisfying assignment, using the reference solver so large traces stay tractable.

    The name is kept for call-site compatibility; enumeration is only used when no reference solver is
    installed, which caps the trace size at BRUTE_FORCE_VARIABLE_LIMIT variables.
    """
    if _REFERENCE_SOLVER is not None:
        return reference_model(_REFERENCE_SOLVER, clauses, assumptions, variable_count)
    return enumerate_model(clauses, assumptions, variable_count)


def failed_assumptions(clauses: list[list[int]], assumptions: list[int], variable_count: int) -> set[int]:
    if not assumptions:
        return set()
    for size in range(1, len(assumptions) + 1):
        for subset in itertools.combinations(range(len(assumptions)), size):
            candidate = [assumptions[index] for index in subset]
            if brute_force_model(clauses, candidate, variable_count) is None:
                return set(candidate)
    return set()


def build_cnf(clauses: list[list[int]], variable_count: int) -> str:
    content = [f"p cnf {variable_count} {len(clauses)}"]
    content.extend(" ".join(str(literal) for literal in clause) + " 0" for clause in clauses)
    return "\n".join(content) + "\n"


def replay(path: Path, command: str) -> tuple[list[str], list[bool | None], list[bool]]:
    lines = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    if not lines:
        return [], [], []
    clauses: list[list[int]] = []
    assumptions: list[int] = []
    statuses: list[str] = []
    value_results: list[bool | None] = []
    failed_results: list[bool] = []
    option_values: dict[str, int] = {}
    last_model: dict[int, bool] | None = None
    last_failed: set[int] = set()
    last_status = "UNKNOWN"
    variable_count = int(lines[0]["variable_count"])
    with tempfile.TemporaryDirectory(prefix="kmx-sat-replay-") as temporary:
        instance = Path(temporary) / "replay.cnf"
        for operation in lines[1:]:
            kind = operation["kind"]
            if kind == "add_clause":
                clauses.append(operation["literals"])
            elif kind == "assume":
                assumptions.extend(operation["literals"])
            elif kind == "release_assumptions":
                assumptions.clear()
            elif kind == "reset_session":
                clauses.clear()
                assumptions.clear()
                last_model = None
                last_failed = set()
                last_status = "UNKNOWN"
            elif kind == "set_option":
                option_values[str(operation["option"])] = int(operation["value"])
            elif kind == "value_of":
                variable = int(operation["variable"])
                if last_model is None or variable < 1 or variable > variable_count:
                    value_results.append(None)
                else:
                    value_results.append(last_model.get(variable))
            elif kind == "failed":
                literal = int(operation["literal"])
                failed_results.append(last_status == "UNSATISFIABLE" and literal in last_failed)
            elif kind == "solve":
                instance.write_text(build_cnf(clauses, variable_count), encoding="ascii")
                decision_limit = int(operation.get("decision_limit", option_values.get("decision_limit", 0)))
                conflict_limit = int(operation.get("conflict_limit", option_values.get("conflict_limit", 0)))
                last_status = solver_status(command, instance, assumptions, decision_limit, conflict_limit)
                statuses.append(last_status)
                if last_status == "SATISFIABLE":
                    last_model = brute_force_model(clauses, assumptions, variable_count)
                    last_failed = set()
                elif last_status == "UNSATISFIABLE":
                    last_model = None
                    last_failed = failed_assumptions(clauses, assumptions, variable_count)
                else:
                    last_model = None
                    last_failed = set()
    return statuses, value_results, failed_results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+", type=Path)
    parser.add_argument("--command", required=True, help="Solver command template using {instance}, {assumptions}, and limits.")
    parser.add_argument("--expect-status", choices=("SATISFIABLE", "UNSATISFIABLE", "UNKNOWN"))
    args = parser.parse_args()
    failures: list[str] = []
    total_solves = 0
    total_values = 0
    total_failed = 0
    for trace in args.traces:
        errors = validate(trace)
        if errors:
            failures.extend(f"{trace}: {error}" for error in errors)
            continue
        statuses, value_results, failed_results = replay(trace, args.command)
        total_solves += len(statuses)
        total_values += len(value_results)
        total_failed += len(failed_results)
        if args.expect_status and (not statuses or statuses[-1] != args.expect_status):
            failures.append(f"{trace}: expected {args.expect_status}, got {statuses[-1] if statuses else 'no solve'}")
        print(
            json.dumps(
                {
                    "trace": str(trace),
                    "statuses": statuses,
                    "value_results": value_results,
                    "failed_results": failed_results,
                },
                sort_keys=True,
            )
        )
    if failures:
        for failure in failures:
            print(f"error: {failure}")
        return 1
    print(
        json.dumps(
            {
                "traces": len(args.traces),
                "solve_operations": total_solves,
                "value_queries": total_values,
                "failed_queries": total_failed,
                "status": "passed",
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
