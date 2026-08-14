#!/usr/bin/env python3
"""Validate JSON Lines output from the long incremental memory campaign."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REQUIRED_SAMPLE_FIELDS = {
    "kind",
    "episode",
    "operation",
    "status",
    "rss_kb",
    "conflicts",
    "decisions",
    "restarts",
    "learned_clauses",
    "reduction_passes",
    "reduced_clauses",
    "deleted_clauses",
    "proof_buffered_payload_bytes",
    "cold_footprint_bytes",
    "report_buffer_size",
    "persisted_configuration",
}
VALID_OPERATIONS = {"assumption_unsat", "release_sat", "reset"}
VALID_STATUSES = {"UNSATISFIABLE", "SATISFIABLE", "RESET"}
COUNTER_FIELDS = {
    "conflicts",
    "decisions",
    "restarts",
    "learned_clauses",
    "reduction_passes",
    "reduced_clauses",
    "deleted_clauses",
    "proof_buffered_payload_bytes",
    "cold_footprint_bytes",
    "report_buffer_size",
}


def validate(path: Path, max_rss_growth_kb: int | None = None,
             max_rss_slope_kb_per_episode: int | None = None) -> list[str]:
    errors: list[str] = []
    try:
        rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    except (OSError, json.JSONDecodeError) as error:
        return [f"cannot read JSONL report: {error}"]
    if not rows:
        return ["empty report"]
    if rows[0] != {"schema": 1, "kind": "header", "episodes": 1000}:
        errors.append("invalid report header")
    samples = rows[1:]
    if not samples:
        errors.append("report has no samples")
        return errors
    initial_rss = samples[0].get("rss_kb")
    peak_rss = 0
    reset_count = 0
    previous_episode = -1
    previous_counters: dict[str, int] | None = None
    last_episode = 0
    for index, sample in enumerate(samples, start=2):
        prefix = f"line {index}"
        if not isinstance(sample, dict) or not REQUIRED_SAMPLE_FIELDS.issubset(sample):
            errors.append(f"{prefix}: missing sample fields")
            continue
        if sample["kind"] != "sample":
            errors.append(f"{prefix}: invalid sample kind")
        if not isinstance(sample["episode"], int) or sample["episode"] < previous_episode:
            errors.append(f"{prefix}: episode index is not monotonic")
        previous_episode = sample["episode"]
        last_episode = sample["episode"]
        if sample["operation"] not in VALID_OPERATIONS or sample["status"] not in VALID_STATUSES:
            errors.append(f"{prefix}: invalid operation/status")
        if sample["operation"] == "reset":
            reset_count += 1
            if sample["status"] != "RESET":
                errors.append(f"{prefix}: reset must have RESET status")
        elif sample["operation"] == "assumption_unsat" and sample["status"] != "UNSATISFIABLE":
            errors.append(f"{prefix}: assumption_unsat must be UNSATISFIABLE")
        elif sample["operation"] == "release_sat" and sample["status"] != "SATISFIABLE":
            errors.append(f"{prefix}: release_sat must be SATISFIABLE")
        numeric_fields = REQUIRED_SAMPLE_FIELDS - {"kind", "operation", "status", "persisted_configuration"}
        for field in numeric_fields:
            if not isinstance(sample[field], int) or sample[field] < 0:
                errors.append(f"{prefix}: {field} must be a non-negative integer")
        if not isinstance(sample["persisted_configuration"], bool) or not sample["persisted_configuration"]:
            errors.append(f"{prefix}: persisted configuration must remain enabled")
        if isinstance(sample["rss_kb"], int):
            peak_rss = max(peak_rss, sample["rss_kb"])
        current_counters = {field: sample[field] for field in COUNTER_FIELDS}
        if sample["operation"] == "reset":
            previous_counters = None
        elif previous_counters is not None:
            for field, previous_value in previous_counters.items():
                if current_counters[field] < previous_value:
                    errors.append(f"{prefix}: {field} is not monotonic within the current segment")
        previous_counters = current_counters
    if reset_count == 0:
        errors.append("report has no reset samples")
    if isinstance(initial_rss, int) and max_rss_growth_kb is not None and peak_rss > initial_rss + max_rss_growth_kb:
        errors.append(f"peak RSS grew by more than {max_rss_growth_kb} KB")
    if isinstance(initial_rss, int) and max_rss_slope_kb_per_episode is not None and last_episode > 0:
        slope = max(0, (peak_rss - initial_rss) // last_episode)
        if slope > max_rss_slope_kb_per_episode:
            errors.append(f"RSS slope exceeded {max_rss_slope_kb_per_episode} KB per episode")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--max-rss-growth-kb", type=int, default=64 * 1024)
    parser.add_argument("--max-rss-slope-kb-per-episode", type=int, default=64)
    args = parser.parse_args()
    errors = validate(args.report, args.max_rss_growth_kb, args.max_rss_slope_kb_per_episode)
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print(json.dumps({"report": str(args.report), "status": "passed"}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
