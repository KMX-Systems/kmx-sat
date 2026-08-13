#!/usr/bin/env python3
"""Run the full debug, release, UBSan, and benchmark acceptance matrix from clean build roots."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(command: list[str]) -> None:
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if completed.returncode != 0:
        raise SystemExit(f"acceptance command failed with exit {completed.returncode}: {' '.join(command)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("clean-checkout-gates"))
    parser.add_argument("--seed", type=int, default=20260814)
    parser.add_argument("--repeat-count", type=int, default=2)
    parser.add_argument("--differential-cases", type=int, default=128)
    args = parser.parse_args()
    if args.repeat_count < 1 or args.differential_cases < 1:
        parser.error("repeat count and differential case count must be positive")

    output_dir = (ROOT / args.output_dir).resolve() if not args.output_dir.is_absolute() else args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    build_roots = {
        "debug": ROOT / "source/build/clean-gate-debug",
        "release": ROOT / "source/build/clean-gate-release",
    }
    for build_root in build_roots.values():
        if build_root.exists():
            shutil.rmtree(build_root)

    summaries: dict[str, dict] = {}
    for variant, build_root in build_roots.items():
        result_dir = output_dir / variant
        run(
            [
                sys.executable,
                "benchmarks/run_release_gates.py",
                "--build",
                "--build-dir",
                str(build_root.relative_to(ROOT)),
                "--build-variant",
                variant,
                *(["--direct-ubsan"] if variant == "debug" else []),
                "--output-dir",
                str(result_dir),
                "--seed",
                str(args.seed),
                "--repeat-count",
                str(args.repeat_count),
                "--differential-cases",
                str(args.differential_cases),
            ]
        )
        summaries[variant] = json.loads((result_dir / "summary.json").read_text(encoding="utf-8"))

    summary = {
        "seed": args.seed,
        "repeat_count": args.repeat_count,
        "differential_cases": args.differential_cases,
        "variants": summaries,
        "status": "passed",
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
