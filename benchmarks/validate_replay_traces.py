#!/usr/bin/env python3
"""Validate persisted incremental replay JSON Lines artifacts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


VALID_KINDS = {"add_clause", "assume", "release_assumptions", "solve", "reset_session", "set_option", "value_of", "failed"}


def validate(path: Path) -> list[str]:
    errors: list[str] = []
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        return ["empty trace"]
    try:
        header = json.loads(lines[0])
    except json.JSONDecodeError as error:
        return [f"invalid header JSON: {error}"]
    if header != {"schema": 1, "seed": header.get("seed"), "variable_count": header.get("variable_count"), "kind": "header"}:
        errors.append("invalid trace header")
    if not isinstance(header.get("seed"), int) or not isinstance(header.get("variable_count"), int):
        errors.append("header seed and variable_count must be integers")
    for index, line in enumerate(lines[1:], start=2):
        try:
            operation = json.loads(line)
        except json.JSONDecodeError as error:
            errors.append(f"line {index}: invalid JSON: {error}")
            continue
        if operation.get("kind") not in VALID_KINDS:
            errors.append(f"line {index}: invalid operation kind")
        if operation.get("kind") in {"add_clause", "assume"}:
            literals = operation.get("literals")
            if not isinstance(literals, list) or not literals or any(not isinstance(value, int) or value == 0 for value in literals):
                errors.append(f"line {index}: invalid literals")
        if operation.get("kind") == "set_option":
            option = operation.get("option")
            value = operation.get("value")
            if not isinstance(option, str) or not option:
                errors.append(f"line {index}: invalid option name")
            if not isinstance(value, int):
                errors.append(f"line {index}: invalid option value")
        if operation.get("kind") == "value_of":
            variable = operation.get("variable")
            if not isinstance(variable, int) or variable <= 0:
                errors.append(f"line {index}: invalid variable")
        if operation.get("kind") == "failed":
            literal = operation.get("literal")
            if not isinstance(literal, int) or literal == 0:
                errors.append(f"line {index}: invalid failed literal")
        if operation.get("kind") == "solve" and any(
            not isinstance(operation.get(field), int) or operation[field] < 0 for field in ("conflict_limit", "decision_limit")
        ):
            errors.append(f"line {index}: invalid solve limits")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+", type=Path)
    args = parser.parse_args()
    errors: list[str] = []
    for trace in args.traces:
        if not trace.is_file():
            errors.append(f"missing trace: {trace}")
            continue
        errors.extend(f"{trace}: {error}" for error in validate(trace))
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print(f"validated {len(args.traces)} replay traces")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
