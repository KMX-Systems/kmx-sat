#!/usr/bin/env python3
"""Record availability of optional external solver and proof-checker tools."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


TOOLS = ("cadical", "kissat", "drat-trim", "lrat-check", "veripb")
ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("external-dependency-report.json"))
    args = parser.parse_args()
    output = args.output if args.output.is_absolute() else ROOT / args.output
    dependencies = {
        name: {"available": shutil.which(name) is not None, "path": shutil.which(name)} for name in TOOLS
    }
    report = {
        "schema": 1,
        "status": "recorded",
        "dependencies": dependencies,
        "unavailable": [name for name, value in dependencies.items() if not value["available"]],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "status": report["status"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
