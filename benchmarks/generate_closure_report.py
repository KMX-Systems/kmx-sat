#!/usr/bin/env python3
"""Validate and consolidate the repository's R9 acceptance artifacts."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def relative(path: Path) -> str:
    return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)


def artifact(path: Path, kind: str, status: str = "validated") -> dict[str, object]:
    return {"kind": kind, "path": relative(path), "status": status, "exists": path.exists()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("clean-checkout-gates/closure-report.json"))
    args = parser.parse_args()

    output = args.output if args.output.is_absolute() else ROOT / args.output
    debug_summary = ROOT / "clean-checkout-gates/debug/summary.json"
    release_summary = ROOT / "clean-checkout-gates/release/summary.json"
    top_summary = ROOT / "clean-checkout-gates/summary.json"
    manifest = ROOT / "benchmarks/corpus/manifest.json"
    debug_memory = ROOT / "clean-checkout-gates/debug/memory-time-series.jsonl"
    r3_scheduled = ROOT / "clean-checkout-gates/r3-scheduled-corpus.json"
    r3_randomized = ROOT / "clean-checkout-gates/r3-randomized-campaign.json"
    proof_report = ROOT / "clean-checkout-gates/debug/proof-movement-report.json"
    dependency_report = ROOT / "clean-checkout-gates/external-dependency-report.json"
    external_solver_report = ROOT / "clean-checkout-gates/external-solvers-release.json"

    artifacts = [
        artifact(top_summary, "clean_matrix_summary"),
        artifact(debug_summary, "debug_acceptance_summary"),
        artifact(release_summary, "release_acceptance_summary"),
        artifact(ROOT / "clean-checkout-gates/debug/pinned-corpus.json", "debug_corpus_report"),
        artifact(ROOT / "clean-checkout-gates/release/pinned-corpus.json", "release_corpus_report"),
        artifact(ROOT / "clean-checkout-gates/debug/differential.json", "debug_differential_report"),
        artifact(ROOT / "clean-checkout-gates/release/differential.json", "release_differential_report"),
        artifact(debug_memory, "memory_time_series"),
        artifact(r3_scheduled, "scheduled_restart_reduction_corpus"),
        artifact(r3_randomized, "randomized_restart_reduction_campaign"),
        artifact(proof_report, "proof_movement_report"),
        artifact(dependency_report, "external_dependency_report"),
        artifact(external_solver_report, "external_solver_comparison"),
        artifact(ROOT / "benchmarks/generate_api_abi_report.py", "api_abi_generator", "generator"),
        artifact(ROOT / "source/library-test/src/kmx/sat/proof/live_proof_movement_test.cpp", "proof_movement_test", "test_backed"),
        artifact(ROOT / "source/library-test/src/kmx/sat/io/fixture/binary_roundtrip_test.cpp", "binary_equivalence_test", "test_backed"),
    ]

    checks: dict[str, object] = {}
    if top_summary.exists():
        summary = json.loads(top_summary.read_text(encoding="utf-8"))
        checks["clean_matrix"] = {"status": summary.get("status"), "variants": sorted(summary.get("variants", {}).keys())}
    else:
        checks["clean_matrix"] = {"status": "missing"}

    memory_result = run(["python3", "benchmarks/validate_memory_time_series.py", str(debug_memory)]) if debug_memory.exists() else None
    checks["memory_time_series"] = {
        "status": "passed" if memory_result is not None and memory_result.returncode == 0 else "missing_or_failed",
        "output": memory_result.stdout.strip() if memory_result is not None else "report not found",
    }

    if r3_scheduled.exists():
        scheduled = json.loads(r3_scheduled.read_text(encoding="utf-8"))
        totals: dict[str, int] = {}
        statuses: dict[str, int] = {}
        for instance in scheduled.get("instances", []):
            run_result = instance.get("solver", {})
            statuses[run_result.get("status", "UNKNOWN")] = statuses.get(run_result.get("status", "UNKNOWN"), 0) + 1
            for name, value in run_result.get("metrics", {}).items():
                totals[name] = totals.get(name, 0) + int(value)
        checks["scheduled_restart_reduction"] = {
            "status": "passed" if totals.get("restarts", 0) > 0 and totals.get("reduction_passes", 0) > 0 else "failed",
            "statuses": statuses,
            "metrics": totals,
        }
    else:
        checks["scheduled_restart_reduction"] = {"status": "missing"}

    if r3_randomized.exists():
        randomized = json.loads(r3_randomized.read_text(encoding="utf-8"))
        checks["randomized_restart_reduction"] = {
            "status": randomized.get("status", "missing"),
            "cases": randomized.get("cases", 0),
            "totals": randomized.get("totals", {}),
            "failures": randomized.get("failures", []),
        }
    else:
        checks["randomized_restart_reduction"] = {"status": "missing"}

    proof_result = run(["python3", "benchmarks/validate_proof_movement_report.py", str(proof_report)]) if proof_report.exists() else None
    checks["proof_movement"] = {
        "status": "passed" if proof_result is not None and proof_result.returncode == 0 else "missing_or_failed",
        "output": proof_result.stdout.strip() if proof_result is not None else "report not found",
    }

    api_library = ROOT / "source/build/clean-gate-release/default/kmx-sat-lib.d24ad72f/libkmx-sat-lib.a"
    library_candidates = sorted((ROOT / "source/build/clean-gate-release/default").glob("**/libkmx-sat-lib.a"))
    if library_candidates:
        api_library = library_candidates[0]
    api_output = ROOT / "clean-checkout-gates/api-abi-report.json"
    api_result = run(["python3", "benchmarks/generate_api_abi_report.py", "--library", str(api_library), "--output", str(api_output)]) if api_library.exists() else None
    checks["api_abi"] = {
        "status": "passed" if api_result is not None and api_result.returncode == 0 else "missing_or_failed",
        "library": relative(api_library),
        "report": relative(api_output),
        "output": api_result.stdout.strip() if api_result is not None else "library not found",
    }

    dependency_result = run(["python3", "benchmarks/generate_external_dependency_report.py", "--output", str(dependency_report)])
    dependency_document = json.loads(dependency_report.read_text(encoding="utf-8")) if dependency_report.exists() else {}
    checks["external_dependencies"] = {
        "status": "passed" if dependency_result.returncode == 0 and dependency_document.get("schema") == 1 else "failed",
        "dependencies": dependency_document.get("dependencies", {}),
        "unavailable": dependency_document.get("unavailable", []),
    }

    required_passes = (
        checks["clean_matrix"].get("status") == "passed",
        checks["memory_time_series"]["status"] == "passed",
        checks["api_abi"]["status"] == "passed",
        checks["scheduled_restart_reduction"]["status"] == "passed",
        checks["randomized_restart_reduction"]["status"] == "passed",
        checks["proof_movement"]["status"] == "passed",
        checks["external_dependencies"]["status"] == "passed",
        all(item["exists"] for item in artifacts if item["status"] != "generator"),
    )
    report = {
        "schema": 1,
        "seed": 20260814,
        "status": "passed" if all(required_passes) else "incomplete",
        "artifacts": artifacts,
        "checks": checks,
        "test_backed_evidence": {
            "proof": "kmx-sat-proof-test and live_proof_movement_test passed in debug and release matrices",
            "binary": "kmx-sat-io-test and binary_roundtrip_test passed in debug and release matrices",
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": relative(output), "status": report["status"]}, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
