#!/usr/bin/env python3
"""Run a seeded real-session restart/reduction campaign over the pinned corpus."""

from __future__ import annotations

import argparse
import json
import random
import re
import shlex
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
METRIC_PATTERN = re.compile(r"\b(conflicts|decisions|propagations|restarts|learned_clauses|reduction_passes|reduced_clauses|deleted_clauses|proof_events)=(\d+)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=Path("benchmarks/corpus/manifest.json"))
    parser.add_argument("--cases", type=int, default=32)
    parser.add_argument("--seed", type=int, default=20260814)
    parser.add_argument("--output", type=Path, default=Path("restart-reduction-campaign.json"))
    args = parser.parse_args()
    if args.cases < 1:
        parser.error("--cases must be positive")

    solver = args.solver if args.solver.is_absolute() else ROOT / args.solver
    manifest_path = args.manifest if args.manifest.is_absolute() else ROOT / args.manifest
    output = args.output if args.output.is_absolute() else ROOT / args.output
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    instances = [ROOT / "benchmarks/corpus" / entry["file"] for entry in manifest["instances"]]
    rng = random.Random(args.seed)
    records: list[dict[str, object]] = []
    totals: dict[str, int] = {}
    failures: list[str] = []

    for index in range(args.cases):
        instance = instances[index % len(instances)]
        options = {
            "restart_interval": rng.randint(1, 4),
            "decision_restart_interval": rng.randint(1, 4),
            "reduction_interval": rng.randint(1, 4),
            "reduction_fraction_percent": rng.choice((25, 50, 75, 100)),
        }
        command = [str(solver), str(instance)]
        for name, value in options.items():
            command.extend([f"--{name.replace('_', '-')}", str(value)])
        completed = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        output_text = completed.stdout
        status = "SATISFIABLE" if "s SATISFIABLE" in output_text else "UNSATISFIABLE" if "s UNSATISFIABLE" in output_text else "UNKNOWN"
        metrics = {name: int(value) for name, value in METRIC_PATTERN.findall(output_text)}
        expected = next(entry["expected_status"] for entry in manifest["instances"] if entry["file"] == instance.name)
        if status != expected:
            failures.append(f"case {index}: {instance.name} expected {expected}, got {status}")
        for name, value in metrics.items():
            totals[name] = totals.get(name, 0) + value
        records.append({"case": index, "instance": str(instance.relative_to(ROOT)), "options": options, "status": status, "expected_status": expected, "metrics": metrics, "exit_code": completed.returncode, "command": shlex.join(command)})

    report = {
        "schema": 1,
        "seed": args.seed,
        "cases": args.cases,
        "status": "passed" if not failures and totals.get("restarts", 0) > 0 and totals.get("reduction_passes", 0) > 0 else "failed",
        "totals": totals,
        "failures": failures,
        "records": records,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"cases": args.cases, "status": report["status"], "totals": totals, "output": str(output)}, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
