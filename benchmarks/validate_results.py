#!/usr/bin/env python3
"""Validate and summarize JSON output produced by run_benchmarks.py."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def validate(document: dict) -> list[str]:
    errors: list[str] = []
    if document.get("schema") != 1:
        errors.append("unsupported or missing schema")
    if not isinstance(document.get("instances"), list):
        return errors + ["instances must be a list"]
    provenance = document.get("provenance")
    if not isinstance(provenance, dict):
        errors.append("provenance must be an object")
    else:
        for field in ("compiler", "compiler_version", "build_mode", "cpu", "os", "revision"):
            if not isinstance(provenance.get(field), str) or not provenance[field]:
                errors.append(f"provenance.{field} must be a non-empty string")
    required_kpi_fields = {
        "solver_conflicts",
        "solver_decisions",
        "solver_propagations",
        "solver_restarts",
        "solver_learned_clauses",
        "solver_elapsed_ms_median",
        "solver_elapsed_ms_p95",
        "solver_propagations_per_second",
        "solver_conflicts_per_second",
        "solver_peak_rss_kb_max",
        "solver_learned_clause_glue_total",
        "solver_learned_clause_glue_samples",
        "solver_average_learned_clause_glue",
        "solver_reduction_passes",
        "solver_reduced_clauses",
        "solver_deleted_clauses",
        "solver_learned_clause_deletion_ratio",
        "solver_proof_events",
        "solver_proof_buffered_payload_bytes_max",
    }
    kpi = document.get("kpi")
    if not isinstance(kpi, dict):
        errors.append("kpi must be an object")
    else:
        missing_kpi = required_kpi_fields - kpi.keys()
        errors.extend(f"kpi missing {field}" for field in sorted(missing_kpi))
        for field in required_kpi_fields:
            if field in kpi and not isinstance(kpi[field], (int, float)):
                errors.append(f"kpi.{field} must be numeric")

    required_result_fields = {"command", "timed_out", "elapsed_ns", "elapsed_ms", "output", "status", "metrics"}
    metric_names = {"conflicts", "decisions", "propagations", "restarts", "learned_clauses", "learned_clause_glue_total", "learned_clause_glue_samples", "reduction_passes", "reduced_clauses", "deleted_clauses", "proof_events", "proof_buffered_payload_bytes"}
    valid_statuses = {"SATISFIABLE", "UNSATISFIABLE", "UNKNOWN"}
    for index, instance in enumerate(document["instances"]):
        prefix = f"instances[{index}]"
        if not isinstance(instance, dict):
            errors.append(f"{prefix} must be an object")
            continue
        for field in ("instance", "instance_sha256", "seed", "timeout_seconds", "solver", "comparisons"):
            if field not in instance:
                errors.append(f"{prefix} missing {field}")
        for field in ("hash_matches_manifest", "status_matches_manifest", "repeats_deterministic"):
            if not isinstance(instance.get(field), bool):
                errors.append(f"{prefix}.{field} must be boolean")
        solver = instance.get("solver")
        if isinstance(solver, dict):
            missing = required_result_fields - solver.keys()
            errors.extend(f"{prefix}.solver missing {field}" for field in sorted(missing))
            if not isinstance(solver.get("elapsed_ns"), int) or solver["elapsed_ns"] < 0:
                errors.append(f"{prefix}.solver elapsed_ns must be non-negative integer")
            if solver.get("status") not in valid_statuses:
                errors.append(f"{prefix}.solver status must be one of {valid_statuses}")
            if not isinstance(solver.get("metrics"), dict) or not metric_names.issubset(solver["metrics"]):
                errors.append(f"{prefix}.solver metrics must contain {sorted(metric_names)}")
        else:
            errors.append(f"{prefix}.solver must be an object")
        runs = instance.get("solver_runs")
        if not isinstance(runs, list) or not runs:
            errors.append(f"{prefix}.solver_runs must be a non-empty list")
        elif document.get("memory_measurement_enabled") and any(
            not isinstance(run.get("peak_rss_kb"), int) or run["peak_rss_kb"] < 0 for run in runs if isinstance(run, dict)
        ):
            errors.append(f"{prefix}.solver_runs peak_rss_kb must be non-negative integers")
        comparisons = instance.get("comparisons")
        if not isinstance(comparisons, dict):
            errors.append(f"{prefix}.comparisons must be an object")
            continue
        for label, comparison_result in comparisons.items():
            if isinstance(comparison_result, dict):
                missing = required_result_fields - comparison_result.keys()
                errors.extend(f"{prefix}.comparisons[{label}] missing {field}" for field in sorted(missing))
                if comparison_result.get("status") not in valid_statuses:
                    errors.append(f"{prefix}.comparisons[{label}] status must be one of {valid_statuses}")
                if not isinstance(comparison_result.get("metrics"), dict):
                    errors.append(f"{prefix}.comparisons[{label}] metrics must be an object")
    return errors


def compare_baseline(document: dict, baseline: dict) -> list[str]:
    errors: list[str] = []
    current = document.get("kpi", {})
    expected_status_counts = baseline.get("status_counts", {})
    actual_status_counts: dict[str, int] = {}
    for item in document.get("instances", []):
        status = item.get("solver", {}).get("status", "UNKNOWN")
        actual_status_counts[status] = actual_status_counts.get(status, 0) + 1
    if expected_status_counts and actual_status_counts != expected_status_counts:
        errors.append(f"status counts differ: expected {expected_status_counts}, got {actual_status_counts}")

    baseline_kpi = baseline.get("kpi", {})
    budgets = baseline.get("budgets", {})
    for metric in ("solver_propagations_per_second", "solver_conflicts_per_second"):
        if metric not in baseline_kpi:
            errors.append(f"baseline missing {metric}")
            continue
        minimum_percent = float(budgets.get(f"{metric}_minimum_percent", 0.0))
        minimum = float(baseline_kpi[metric]) * minimum_percent / 100.0
        if float(current.get(metric, 0.0)) < minimum:
            errors.append(f"{metric} below budget: minimum {minimum}, got {current.get(metric)}")

    latency_metric = "solver_elapsed_ms_p95"
    if latency_metric in baseline_kpi:
        maximum_percent = float(budgets.get(f"{latency_metric}_maximum_percent", 100.0))
        maximum = float(baseline_kpi[latency_metric]) * maximum_percent / 100.0
        if float(current.get(latency_metric, math.inf)) > maximum:
            errors.append(f"{latency_metric} above budget: maximum {maximum}, got {current.get(latency_metric)}")
    memory_metric = "solver_peak_rss_kb_max"
    if memory_metric in baseline_kpi:
        maximum_percent = float(budgets.get("solver_peak_rss_kb_maximum_percent", 100.0))
        maximum = float(baseline_kpi[memory_metric]) * maximum_percent / 100.0
        if float(current.get(memory_metric, math.inf)) > maximum:
            errors.append(f"{memory_metric} above budget: maximum {maximum}, got {current.get(memory_metric)}")
    glue_metric = "solver_average_learned_clause_glue"
    if glue_metric in baseline_kpi:
        maximum_percent = float(budgets.get("solver_average_learned_clause_glue_maximum_percent", 100.0))
        maximum = float(baseline_kpi[glue_metric]) * maximum_percent / 100.0
        if float(current.get(glue_metric, math.inf)) > maximum:
            errors.append(f"{glue_metric} above budget: maximum {maximum}, got {current.get(glue_metric)}")
    for metric, budget_key in (
        ("solver_proof_events", "solver_proof_events_maximum_percent"),
        ("solver_proof_buffered_payload_bytes_max", "solver_proof_buffered_payload_bytes_maximum_percent"),
    ):
        if metric in baseline_kpi:
            maximum_percent = float(budgets.get(budget_key, 100.0))
            maximum = float(baseline_kpi[metric]) * maximum_percent / 100.0
            if float(current.get(metric, math.inf)) > maximum:
                errors.append(f"{metric} above budget: maximum {maximum}, got {current.get(metric)}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result", type=Path)
    parser.add_argument("--baseline", type=Path, help="Optional KPI baseline and regression-budget document.")
    args = parser.parse_args()
    document = json.loads(args.result.read_text(encoding="utf-8"))
    errors = validate(document)
    if args.baseline:
        errors.extend(compare_baseline(document, json.loads(args.baseline.read_text(encoding="utf-8"))))
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1

    durations = [item["solver"]["elapsed_ms"] for item in document["instances"]]
    status_counts = {}
    for item in document["instances"]:
        status = item["solver"].get("status", "UNKNOWN")
        status_counts[status] = status_counts.get(status, 0) + 1
    summary = {
        "instances": len(durations),
        "total_elapsed_ms": sum(durations),
        "max_elapsed_ms": max(durations, default=0.0),
        "timed_out": sum(bool(item["solver"]["timed_out"]) for item in document["instances"]),
        "status_counts": status_counts,
    }
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
