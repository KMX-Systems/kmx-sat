#!/usr/bin/env python3
"""Decide CNF instances with a vendored reference solver, falling back to brute force for tiny inputs.

The brute-force oracle used previously is capped at 2**n enumeration, which held the correctness campaigns
at four variables -- far below the sizes where search bugs actually appear. Routing the oracle through
kissat/cadical (already vendored under tools/bin) removes that cap.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_SOLVER_CANDIDATES = ("tools/bin/kissat", "tools/bin/cadical")
BRUTE_FORCE_VARIABLE_LIMIT = 20


def find_reference_solver() -> str | None:
    """Locate a vendored or installed reference solver to use as the oracle."""
    for candidate in REFERENCE_SOLVER_CANDIDATES:
        vendored = ROOT / candidate
        if vendored.is_file() and os.access(vendored, os.X_OK):
            return str(vendored)
        installed = shutil.which(Path(candidate).name)
        if installed is not None:
            return installed
    return None


def write_dimacs(path: Path, clauses: list[list[int]], variable_count: int) -> None:
    lines = [f"p cnf {variable_count} {len(clauses)}"]
    lines.extend(" ".join(str(literal) for literal in clause) + " 0" for clause in clauses)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def parse_model_values(output: str) -> dict[int, bool]:
    """Collect an assignment from a solver's DIMACS 'v' lines."""
    values: dict[int, bool] = {}
    for line in output.splitlines():
        tokens = line.split()
        if not tokens or tokens[0] != "v":
            continue
        for token in tokens[1:]:
            try:
                value = int(token)
            except ValueError:
                continue
            if value != 0:
                values[abs(value)] = value > 0
    return values


def model_satisfies(clauses: list[list[int]], assumptions: list[int], values: dict[int, bool]) -> bool:
    """Check an assignment against clauses and assumptions, treating absent variables as true."""
    for clause in clauses + [[literal] for literal in assumptions]:
        if not any(values.get(abs(literal), True) == (literal > 0) for literal in clause):
            return False
    return True


def brute_force_model(clauses: list[list[int]], assumptions: list[int], variable_count: int) -> dict[int, bool] | None:
    for assignment in range(1 << variable_count):
        values = {index: bool((assignment >> (index - 1)) & 1) for index in range(1, variable_count + 1)}
        if model_satisfies(clauses, assumptions, values):
            return values
    return None


def reference_model(executable: str, clauses: list[list[int]], assumptions: list[int],
                    variable_count: int) -> dict[int, bool] | None:
    """Return a satisfying assignment from the reference solver, or None when the instance is unsatisfiable."""
    with tempfile.TemporaryDirectory(prefix="kmx-sat-oracle-") as temporary:
        path = Path(temporary) / "oracle.cnf"
        write_dimacs(path, clauses + [[literal] for literal in assumptions], variable_count)
        completed = subprocess.run([executable, str(path)], capture_output=True, text=True, check=False)
    if "s UNSATISFIABLE" in completed.stdout:
        return None
    if "s SATISFIABLE" in completed.stdout:
        values = parse_model_values(completed.stdout)
        for index in range(1, variable_count + 1):
            values.setdefault(index, True)
        return values
    raise RuntimeError(f"reference solver {executable} gave no verdict")


def oracle_model(clauses: list[list[int]], assumptions: list[int], variable_count: int,
                 reference: str | None = None) -> dict[int, bool] | None:
    """Return a satisfying assignment, or None when unsatisfiable."""
    executable = reference if reference is not None else find_reference_solver()
    if executable is not None:
        return reference_model(executable, clauses, assumptions, variable_count)
    if variable_count > BRUTE_FORCE_VARIABLE_LIMIT:
        raise RuntimeError(
            f"no reference solver available and {variable_count} variables exceeds the brute-force limit of "
            f"{BRUTE_FORCE_VARIABLE_LIMIT}; install kissat or cadical under tools/bin"
        )
    return brute_force_model(clauses, assumptions, variable_count)


def oracle_status(clauses: list[list[int]], assumptions: list[int], variable_count: int,
                  reference: str | None = None) -> str:
    """Return SATISFIABLE or UNSATISFIABLE for the instance."""
    return "UNSATISFIABLE" if oracle_model(clauses, assumptions, variable_count, reference) is None else "SATISFIABLE"
