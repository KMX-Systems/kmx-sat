#!/usr/bin/env python3
"""Run a seeded small-CNF differential campaign against a brute-force oracle."""

from __future__ import annotations

import argparse
import json
import os
import random
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))


from reference_oracle import (  # noqa: E402
    BRUTE_FORCE_VARIABLE_LIMIT,
    find_reference_solver,
    model_satisfies,
    oracle_status,
    parse_model_values,
)


def oracle(clauses: list[list[int]], assumptions: list[int], variable_count: int,
           reference: str | None = None, workdir: Path | None = None) -> str:
    """Decide an instance, preferring a reference solver over 2**n enumeration."""
    return oracle_status(clauses, assumptions, variable_count, reference)


def write_dimacs(path: Path, clauses: list[list[int]], variable_count: int) -> None:
    lines = [f"p cnf {variable_count} {len(clauses)}"]
    lines.extend(" ".join(str(literal) for literal in clause) + " 0" for clause in clauses)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def write_replay_trace(
    path: Path,
    seed: int,
    variable_count: int,
    clauses: list[list[int]],
    assumptions: list[int],
    decision_limit: int,
    conflict_limit: int,
) -> None:
    lines = [json.dumps({"schema": 1, "seed": seed, "variable_count": variable_count, "kind": "header"}, separators=(",", ":"))]
    lines.extend(json.dumps({"kind": "add_clause", "literals": clause}, separators=(",", ":")) for clause in clauses)
    lines.extend(json.dumps({"kind": "assume", "literals": [literal]}, separators=(",", ":")) for literal in assumptions)
    lines.append(
        json.dumps(
            {
                "kind": "solve",
                "conflict_limit": conflict_limit,
                "decision_limit": decision_limit,
            },
            separators=(",", ":"),
        )
    )
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def solver_status(command: str, instance: Path, assumptions: list[int], decision_limit: int, conflict_limit: int) -> tuple[str, str]:
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
    output = completed.stdout
    if "s SATISFIABLE" in output:
        return "SATISFIABLE", output
    if "s UNSATISFIABLE" in output:
        return "UNSATISFIABLE", output
    return "UNKNOWN", output


def model_is_valid(output: str, clauses: list[list[int]], assumptions: list[int]) -> bool | None:
    """Check the solver's `v` line against the instance; None when no model was emitted."""
    values = parse_model_values(output)
    if not values:
        return None
    return model_satisfies(clauses, assumptions, values)


def shrink(
    clauses: list[list[int]],
    assumptions: list[int],
    variable_count: int,
    expected: str,
    command: str,
    decision_limit: int,
    conflict_limit: int,
    workdir: Path,
) -> tuple[list[list[int]], list[int]]:
    candidate = [clause[:] for clause in clauses]
    candidate_assumptions = assumptions[:]

    def preserves_failure(test_clauses: list[list[int]], test_assumptions: list[int]) -> bool:
        path = workdir / "shrink.cnf"
        write_dimacs(path, test_clauses, variable_count)
        actual, _ = solver_status(command, path, test_assumptions, decision_limit, conflict_limit)
        limited = decision_limit != 0 or conflict_limit != 0
        return not (actual == expected or (limited and actual == "UNKNOWN"))

    changed = True
    while changed:
        changed = False
        for index in range(len(candidate)):
            reduced = candidate[:index] + candidate[index + 1 :]
            if not reduced:
                continue
            if preserves_failure(reduced, candidate_assumptions):
                candidate = reduced
                changed = True
                break
        if changed:
            continue
        for clause_index, clause in enumerate(candidate):
            if len(clause) <= 1:
                continue
            for literal_index in range(len(clause)):
                reduced_clause = clause[:literal_index] + clause[literal_index + 1 :]
                reduced = candidate[:]
                reduced[clause_index] = reduced_clause
                if preserves_failure(reduced, candidate_assumptions):
                    candidate = reduced
                    changed = True
                    break
            if changed:
                break
        if changed:
            continue
        for assumption_index in range(len(candidate_assumptions)):
            reduced_assumptions = candidate_assumptions[:assumption_index] + candidate_assumptions[assumption_index + 1 :]
            if preserves_failure(candidate, reduced_assumptions):
                candidate_assumptions = reduced_assumptions
                changed = True
                break
    return candidate, candidate_assumptions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--command",
        required=True,
        help="Solver command template using {instance}, {assumptions}, {decision_limit}, and {conflict_limit}.",
    )
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--cases", type=int, default=100)
    parser.add_argument("--variables", type=int, help="Fixed variable count; overrides --variable-ladder.")
    parser.add_argument("--clauses", type=int, help="Fixed clause count; overrides --clause-ratio.")
    parser.add_argument(
        "--variable-ladder",
        default="4,10,25,60,120,150",
        help="Comma-separated variable counts cycled across cases; small rungs stay cheap to shrink, "
             "large rungs reach the sizes where search bugs actually appear.",
    )
    parser.add_argument("--clause-ratio", type=float, default=4.26,
                        help="Clauses per variable when --clauses is not given (4.26 is the 3-SAT threshold).")
    parser.add_argument("--output", type=Path, default=Path("differential-results.json"))
    parser.add_argument("--replay-dir", type=Path, help="Directory for JSON Lines replay artifacts.")
    args = parser.parse_args()
    if args.cases < 1:
        parser.error("cases must be positive")
    if args.variables is not None and args.variables < 1:
        parser.error("variables must be positive")
    if args.clauses is not None and args.clauses < 1:
        parser.error("clauses must be positive")
    if args.clause_ratio <= 0.0:
        parser.error("clause ratio must be positive")

    if args.variables is not None:
        variable_ladder = [args.variables]
    else:
        try:
            variable_ladder = [int(rung) for rung in args.variable_ladder.split(",") if rung.strip()]
        except ValueError:
            parser.error(f"invalid --variable-ladder value: {args.variable_ladder!r}")
        if not variable_ladder or any(rung < 1 for rung in variable_ladder):
            parser.error("--variable-ladder must list positive integers")

    reference_solver = find_reference_solver()
    if reference_solver is None and max(variable_ladder) > BRUTE_FORCE_VARIABLE_LIMIT:
        parser.error(
            f"no reference solver found under tools/bin and the ladder reaches {max(variable_ladder)} variables, "
            f"above the brute-force limit of {BRUTE_FORCE_VARIABLE_LIMIT}"
        )
    replay_dir = args.replay_dir or args.output.parent / "replays"
    replay_dir.mkdir(parents=True, exist_ok=True)

    rng = random.Random(args.seed)
    with __import__("tempfile").TemporaryDirectory(prefix="kmx-sat-differential-") as temporary:
        workdir = Path(temporary)
        cases: list[dict[str, object]] = []
        failures: list[dict[str, object]] = []
        for case_index in range(args.cases):
            variables = variable_ladder[case_index % len(variable_ladder)]
            # Straddle the 3-SAT threshold so the campaign produces both satisfiable and unsatisfiable
            # instances; a fixed ratio of mixed-width clauses lands almost everything above threshold and
            # would leave the satisfiable path (and the model check with it) untested.
            case_ratio = args.clause_ratio * rng.uniform(0.45, 1.10)
            clause_count = args.clauses if args.clauses is not None else max(1, round(variables * case_ratio))
            clauses = []
            for _ in range(clause_count):
                size = 3 if rng.randrange(10) < 8 else rng.randint(1, 3)
                size = min(size, variables)
                clause = []
                while len(clause) < size:
                    literal = rng.randint(1, variables) * (-1 if rng.randrange(2) else 1)
                    if literal not in clause and -literal not in clause:
                        clause.append(literal)
                clauses.append(clause)
            assumption_count = rng.randint(0, min(2, variables))
            assumptions = []
            while len(assumptions) < assumption_count:
                literal = rng.randint(1, variables) * (-1 if rng.randrange(2) else 1)
                if literal not in assumptions:
                    assumptions.append(literal)
            decision_limit = 1 if rng.randrange(8) == 0 else 0
            conflict_limit = 1 if decision_limit == 0 and rng.randrange(8) == 0 else 0
            expected = oracle(clauses, assumptions, variables, reference_solver, workdir)
            instance = workdir / f"case-{case_index}.cnf"
            write_dimacs(instance, clauses, variables)
            replay_path = replay_dir / f"case-{case_index}.jsonl"
            write_replay_trace(replay_path, args.seed, variables, clauses, assumptions, decision_limit, conflict_limit)
            actual, solver_output = solver_status(args.command, instance, assumptions, decision_limit, conflict_limit)
            limited = decision_limit != 0 or conflict_limit != 0
            model_valid = model_is_valid(solver_output, clauses, assumptions) if actual == "SATISFIABLE" else None
            accepted = actual == expected or (limited and actual == "UNKNOWN")
            # A SATISFIABLE verdict is only accepted when it comes with a model that actually satisfies the instance.
            if actual == "SATISFIABLE" and model_valid is not True:
                accepted = False
            operations: list[dict[str, object]] = []
            operations.extend({"kind": "add_clause", "literals": clause[:]} for clause in clauses)
            operations.extend({"kind": "assume", "literal": literal} for literal in assumptions)
            operations.append(
                {
                    "kind": "solve",
                    "decision_limit": decision_limit,
                    "conflict_limit": conflict_limit,
                    "expected": expected,
                    "actual": actual,
                }
            )
            case = {
                "index": case_index,
                "clauses": clauses,
                "assumptions": assumptions,
                "variables": variables,
                "decision_limit": decision_limit,
                "conflict_limit": conflict_limit,
                "expected": expected,
                "actual": actual,
                "model_valid": model_valid,
                "accepted": accepted,
                "operations": operations,
                "replay_trace": str(replay_path),
            }
            cases.append(case)
            if not accepted:
                minimized_clauses, minimized_assumptions = shrink(
                    clauses,
                    assumptions,
                    variables,
                    expected,
                    args.command,
                    decision_limit,
                    conflict_limit,
                    workdir,
                )
                failures.append(
                    {
                        **case,
                        "minimized_clauses": minimized_clauses,
                        "minimized_assumptions": minimized_assumptions,
                    }
                )
                failure_trace = replay_dir / f"failure-{case_index}.jsonl"
                write_replay_trace(
                    failure_trace,
                    args.seed,
                    variables,
                    minimized_clauses,
                    minimized_assumptions,
                    decision_limit,
                    conflict_limit,
                )
                failures[-1]["minimized_replay_trace"] = str(failure_trace)

    document = {
        "schema": 1,
        "seed": args.seed,
        "variable_ladder": variable_ladder,
        "clause_ratio": args.clause_ratio,
        "oracle": reference_solver or "brute-force",
        "cases": cases,
        "failures": failures,
    }
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"cases": len(cases), "failures": len(failures), "output": str(args.output)}, sort_keys=True))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())