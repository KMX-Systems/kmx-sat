#!/usr/bin/env python3
"""Replay persisted incremental JSON Lines traces through a command-driven solver."""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import tempfile
from pathlib import Path

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


def replay(path: Path, command: str) -> list[str]:
    lines = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    clauses: list[list[int]] = []
    assumptions: list[int] = []
    statuses: list[str] = []
    variable_count = lines[0]["variable_count"]
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
            elif kind == "solve":
                content = [f"p cnf {variable_count} {len(clauses)}"]
                content.extend(" ".join(str(literal) for literal in clause) + " 0" for clause in clauses)
                instance.write_text("\n".join(content) + "\n", encoding="ascii")
                statuses.append(
                    solver_status(command, instance, assumptions, operation["decision_limit"], operation["conflict_limit"])
                )
        return statuses


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+", type=Path)
    parser.add_argument("--command", required=True, help="Solver command template using {instance}, {assumptions}, and limits.")
    parser.add_argument("--expect-status", choices=("SATISFIABLE", "UNSATISFIABLE", "UNKNOWN"))
    args = parser.parse_args()
    failures: list[str] = []
    total_solves = 0
    for trace in args.traces:
        errors = validate(trace)
        if errors:
            failures.extend(f"{trace}: {error}" for error in errors)
            continue
        statuses = replay(trace, args.command)
        total_solves += len(statuses)
        if args.expect_status and (not statuses or statuses[-1] != args.expect_status):
            failures.append(f"{trace}: expected {args.expect_status}, got {statuses[-1] if statuses else 'no solve'}")
        print(json.dumps({"trace": str(trace), "statuses": statuses}, sort_keys=True))
    if failures:
        for failure in failures:
            print(f"error: {failure}")
        return 1
    print(json.dumps({"traces": len(args.traces), "solve_operations": total_solves, "status": "passed"}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
