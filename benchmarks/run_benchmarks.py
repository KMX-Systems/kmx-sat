#!/usr/bin/env python3
"""Run reproducible command-driven SAT benchmark campaigns and emit JSON results."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import signal
import shlex
import shutil
import subprocess
import statistics
import sys
import time
import re
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


_CLAUSE_CACHE: dict[str, list[list[int]]] = {}


def parse_dimacs_clauses(path: Path) -> list[list[int]]:
    """Parse a DIMACS CNF into clauses, stopping at the SATLIB '%' trailer."""
    clauses: list[list[int]] = []
    current: list[int] = []
    with path.open("r", encoding="ascii", errors="replace") as stream:
        for line in stream:
            line = line.strip()
            if not line:
                continue
            if line[0] == "%":
                break
            if line[0] in "cp":
                continue
            for token in line.split():
                value = int(token)
                if value == 0:
                    clauses.append(current)
                    current = []
                else:
                    current.append(value)
    if current:
        clauses.append(current)
    return clauses


def instance_clauses(instance: Path) -> list[list[int]]:
    key = str(instance)
    cached = _CLAUSE_CACHE.get(key)
    if cached is None:
        cached = parse_dimacs_clauses(instance)
        _CLAUSE_CACHE[key] = cached
    return cached


def parse_model_values(output: str) -> dict[int, bool]:
    """Collect the assignment from the solver's DIMACS 'v' lines."""
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


def verify_reported_model(instance: Path, output: str) -> bool | None:
    """Check a reported model against the instance.

    Returns True when every clause is satisfied, False when the model is invalid, and None when the solver
    emitted no model at all (so the answer is unverifiable rather than wrong).
    """
    values = parse_model_values(output)
    if not values:
        return None
    for clause in instance_clauses(instance):
        if not any(values.get(abs(literal), True) == (literal > 0) for literal in clause):
            return False
    return True


def run_command(template: str, instance: Path, seed: int, timeout: float, measure_memory: bool, cpu: int | None = None) -> dict[str, object]:
    command = template.format(instance=shlex.quote(str(instance)), seed=seed, timeout=timeout)
    if cpu is not None:
        command = f"taskset -c {cpu} sh -c {shlex.quote(command)}"
    measured_command = command
    if measure_memory:
        measured_command = f"/usr/bin/time -f '\\nkmx-memory-kb=%M' sh -c {shlex.quote(command)}"
    started = time.perf_counter_ns()
    process = subprocess.Popen(
        measured_command,
        shell=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        output, _ = process.communicate(timeout=timeout)
        timed_out = False
        exit_code = process.returncode
    except subprocess.TimeoutExpired as error:
        timed_out = True
        exit_code = None
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            output += process.communicate(timeout=1.0)[0] or ""
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output += process.communicate()[0] or ""
    elapsed_ns = time.perf_counter_ns() - started
    status = "UNKNOWN"
    if "s SATISFIABLE" in output:
        status = "SATISFIABLE"
    elif "s UNSATISFIABLE" in output:
        status = "UNSATISFIABLE"
    metrics = {name: int(value) for name, value in re.findall(
        r"\b(conflicts|decisions|propagations|restarts|learned_clauses|learned_clause_glue_total|learned_clause_glue_samples|reduction_passes|reduced_clauses|deleted_clauses|proof_events|proof_buffered_payload_bytes)=(\d+)", output
    )}
    memory_match = re.search(r"\bkmx-memory-kb=(\d+)", output)
    model_verified = verify_reported_model(instance, output) if status == "SATISFIABLE" else None
    return {
        "command": command,
        "exit_code": exit_code,
        "timed_out": timed_out,
        "elapsed_ns": elapsed_ns,
        "elapsed_ms": elapsed_ns / 1_000_000.0,
        "output": output[-8192:],
        "status": status,
        "model_verified": model_verified,
        "metrics": metrics,
        "peak_rss_kb": int(memory_match.group(1)) if memory_match else None,
    }


def command_availability(template: str) -> dict[str, object]:
    """Report whether the command's first executable token is available."""
    try:
        tokens = shlex.split(template.format(instance="instance.cnf", seed=1, timeout=1.0))
    except ValueError as error:
        return {"available": False, "executable": None, "error": str(error)}
    if not tokens:
        return {"available": False, "executable": None, "error": "empty command"}
    executable = tokens[0]
    resolved = shutil.which(executable)
    return {"available": resolved is not None, "executable": executable, "resolved": resolved}


def load_manifest(path: Path) -> dict[str, dict[str, str]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    return {entry["file"]: entry for entry in document.get("instances", [])}


def command_output(command: list[str], fallback: str = "unknown") -> str:
    try:
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    except OSError:
        return fallback
    return completed.stdout.splitlines()[0].strip() if completed.stdout else fallback


def provenance(configuration: str) -> dict[str, str]:
    revision = os.environ.get("GIT_COMMIT") or command_output(["git", "rev-parse", "HEAD"])
    compiler = os.environ.get("CXX", "g++")
    return {
        "compiler": compiler,
        "compiler_version": command_output([compiler, "--version"]),
        "build_mode": configuration,
        "cpu": platform.processor() or platform.machine(),
        "os": platform.platform(),
        "revision": revision,
    }


def repeat_signature(result: dict[str, object]) -> tuple[object, object, object]:
    return result["status"], result["model_verified"], tuple(sorted(result["metrics"].items()))


def representative_run(runs: list[dict[str, object]]) -> dict[str, object]:
    completed_runs = [run for run in runs if not run["timed_out"] and run["status"] != "UNKNOWN"]
    timing_runs = completed_runs or runs
    elapsed_values = [float(run["elapsed_ms"]) for run in timing_runs]
    median_ms = statistics.median(elapsed_values)
    representative = min(timing_runs, key=lambda run: abs(float(run["elapsed_ms"]) - median_ms)).copy()
    ordered = sorted(elapsed_values)
    representative["elapsed_ms_min"] = min(ordered)
    representative["elapsed_ms_median"] = median_ms
    representative["elapsed_ms_p90"] = ordered[min(len(ordered) - 1, int(len(ordered) * 0.9))]
    representative["elapsed_ms"] = median_ms
    representative["elapsed_ns"] = int(median_ms * 1_000_000.0)
    representative["completed_run_count"] = len(completed_runs)
    representative["timed_out_run_count"] = sum(1 for run in runs if run["timed_out"])
    return representative


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("instances", nargs="+", type=Path)
    parser.add_argument("--command", required=True, help="Command template; use {instance}, {seed}, and {timeout}.")
    parser.add_argument("--compare-command", action="append", default=[], help="Optional labelled comparison: label=command-template.")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--repeat-count", type=int, default=1)
    parser.add_argument("--warmup-count", type=int, default=2)
    parser.add_argument("--cpu", type=int, help="Pin benchmark processes to one logical CPU with taskset.")
    parser.add_argument("--configuration", default="default")
    parser.add_argument("--output", type=Path, default=Path("benchmark-results.json"))
    parser.add_argument("--manifest", type=Path, help="Optional pinned corpus manifest with expected_status fields.")
    parser.add_argument("--require-comparison-agreement", action="store_true", help="Fail when available comparison status differs from the primary solver.")
    parser.add_argument("--no-memory", action="store_true", help="Disable /usr/bin/time peak RSS measurement.")
    args = parser.parse_args()
    if args.repeat_count < 1 or args.warmup_count < 0:
        parser.error("--repeat-count must be at least 1 and --warmup-count must be non-negative")
    if args.cpu is not None and shutil.which("taskset") is None:
        parser.error("--cpu requires taskset")
    manifest = load_manifest(args.manifest) if args.manifest else {}
    measure_memory = not args.no_memory and Path("/usr/bin/time").is_file()

    comparisons: list[tuple[str, str]] = []
    for item in args.compare_command:
        label, separator, template = item.partition("=")
        if not separator or not label or not template:
            parser.error(f"invalid --compare-command value: {item!r}; expected label=template")
        comparisons.append((label, template))

    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    results: list[dict[str, object]] = []
    for instance in args.instances:
        if not instance.is_file():
            parser.error(f"benchmark instance does not exist: {instance}")
        manifest_entry = manifest.get(instance.name)
        if args.manifest and manifest_entry is None:
            parser.error(f"instance is not listed in manifest: {instance}")
        instance_hash = sha256_file(instance)
        hash_matches_manifest = not manifest_entry or instance_hash == manifest_entry.get("sha256")
        record: dict[str, object] = {
            "instance": str(instance),
            "instance_sha256": instance_hash,
            "seed": args.seed,
            "timeout_seconds": args.timeout,
            "expected_status": manifest_entry.get("expected_status") if manifest_entry else None,
            "solver": {},
            "comparisons": {},
            "solver_command": command_availability(args.command),
            "hash_matches_manifest": hash_matches_manifest,
        }
        for warmup in range(args.warmup_count):
            run_command(args.command, instance, args.seed + warmup, args.timeout, measure_memory, args.cpu)
        solver_runs = [run_command(args.command, instance, args.seed + args.warmup_count + repeat, args.timeout, measure_memory, args.cpu)
                       for repeat in range(args.repeat_count)]
        record["solver_runs"] = solver_runs
        record["solver"] = representative_run(solver_runs)
        if manifest_entry and record["solver"]["status"] != manifest_entry["expected_status"]:
            record["status_matches_manifest"] = False
        else:
            record["status_matches_manifest"] = True
        record["repeats_deterministic"] = all(repeat_signature(run) == repeat_signature(solver_runs[0]) for run in solver_runs[1:])
        # A SATISFIABLE answer counts as verified only when the solver emitted a model and that model checks out.
        record["model_verified"] = all(
            run["model_verified"] is True for run in solver_runs if run["status"] == "SATISFIABLE"
        )
        comparison_records = record["comparisons"]
        assert isinstance(comparison_records, dict)
        for label, template in comparisons:
            for warmup in range(args.warmup_count):
                run_command(template, instance, args.seed + warmup, args.timeout, measure_memory, args.cpu)
            comparison_runs = [run_command(template, instance, args.seed + args.warmup_count + repeat, args.timeout, measure_memory, args.cpu)
                               for repeat in range(args.repeat_count)]
            comparison_records[label] = representative_run(comparison_runs)
            record.setdefault("comparison_commands", {})[label] = command_availability(template)
            record.setdefault("comparison_runs", {})[label] = comparison_runs
        comparison_agreement = True
        if args.require_comparison_agreement:
            for label, comparison_result in comparison_records.items():
                available = record["comparison_commands"][label].get("available", False)
                if available and comparison_result.get("status") != record["solver"].get("status"):
                    comparison_agreement = False
        record["comparison_status_agreement"] = comparison_agreement
        results.append(record)

    document = {
        "schema": 1,
        "started_at_utc": started_at,
        "configuration": args.configuration,
        "revision": os.environ.get("GIT_COMMIT", "unknown"),
        "platform": platform.platform(),
        "python": sys.version,
        "provenance": provenance(args.configuration),
        "instances": results,
        "manifest_enforced": bool(args.manifest),
        "memory_measurement_enabled": measure_memory,
        "warmup_count": args.warmup_count,
        "cpu_affinity": args.cpu,
        "kpi": {
            "solver_conflicts": sum(item["solver"]["metrics"].get("conflicts", 0) for item in results),
            "solver_decisions": sum(item["solver"]["metrics"].get("decisions", 0) for item in results),
            "solver_propagations": sum(item["solver"]["metrics"].get("propagations", 0) for item in results),
            "solver_restarts": sum(item["solver"]["metrics"].get("restarts", 0) for item in results),
            "solver_learned_clauses": sum(item["solver"]["metrics"].get("learned_clauses", 0) for item in results),
            "solver_learned_clause_glue_total": sum(item["solver"]["metrics"].get("learned_clause_glue_total", 0) for item in results),
            "solver_learned_clause_glue_samples": sum(item["solver"]["metrics"].get("learned_clause_glue_samples", 0) for item in results),
            "solver_elapsed_ms_median": statistics.median(item["solver"]["elapsed_ms"] for item in results),
            "solver_elapsed_ms_p95": (statistics.quantiles([item["solver"]["elapsed_ms"] for item in results], n=20, method="inclusive")[18]
                                      if len(results) > 1 else results[0]["solver"]["elapsed_ms"]),
        },
    }
    elapsed_seconds = sum(item["solver"]["elapsed_ns"] for item in results) / 1_000_000_000.0
    document["kpi"]["solver_propagations_per_second"] = (
        document["kpi"]["solver_propagations"] / elapsed_seconds if elapsed_seconds > 0.0 else 0.0
    )
    document["kpi"]["solver_conflicts_per_second"] = (
        document["kpi"]["solver_conflicts"] / elapsed_seconds if elapsed_seconds > 0.0 else 0.0
    )
    document["kpi"]["solver_average_learned_clause_glue"] = (
        document["kpi"]["solver_learned_clause_glue_total"] / document["kpi"]["solver_learned_clause_glue_samples"]
        if document["kpi"]["solver_learned_clause_glue_samples"] > 0 else 0.0
    )
    document["kpi"]["solver_reduction_passes"] = sum(item["solver"]["metrics"].get("reduction_passes", 0) for item in results)
    document["kpi"]["solver_reduced_clauses"] = sum(item["solver"]["metrics"].get("reduced_clauses", 0) for item in results)
    document["kpi"]["solver_deleted_clauses"] = sum(item["solver"]["metrics"].get("deleted_clauses", 0) for item in results)
    document["kpi"]["solver_learned_clause_deletion_ratio"] = (
        document["kpi"]["solver_deleted_clauses"] / document["kpi"]["solver_learned_clauses"]
        if document["kpi"]["solver_learned_clauses"] > 0 else 0.0
    )
    document["kpi"]["solver_proof_events"] = sum(item["solver"]["metrics"].get("proof_events", 0) for item in results)
    document["kpi"]["solver_proof_buffered_payload_bytes_max"] = max(
        (item["solver"]["metrics"].get("proof_buffered_payload_bytes", 0) for item in results), default=0
    )
    document["kpi"]["solver_models_verified"] = sum(1 for item in results if item["model_verified"])
    document["kpi"]["solver_models_unverified"] = sum(1 for item in results if not item["model_verified"])
    memory_values = [run["peak_rss_kb"] for item in results for run in item["solver_runs"] if run["peak_rss_kb"] is not None]
    document["kpi"]["solver_peak_rss_kb_max"] = max(memory_values, default=0)
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "instances": len(results)}, sort_keys=True))
    return 1 if any(
        not item["status_matches_manifest"] or not item["hash_matches_manifest"] or not item["repeats_deterministic"]
        or not item["comparison_status_agreement"] or not item["model_verified"]
        for item in results
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
