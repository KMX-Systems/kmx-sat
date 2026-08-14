#!/usr/bin/env python3
"""Validate the optional live proof and movement integration report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    document = json.loads(args.report.read_text(encoding="utf-8"))
    required = ("schema", "status", "proof_checked", "original_events", "derived_events", "conclusion_events", "antecedent_ids")
    if document.get("schema") != 1 or any(field not in document for field in required):
        print("error: invalid proof report schema")
        return 1
    errors = []
    if document["status"] != "UNSATISFIABLE":
        errors.append("live proof status is not UNSATISFIABLE")
    if not document["proof_checked"]:
        errors.append("proof checker did not pass")
    if document["original_events"] == 0 or document["derived_events"] == 0:
        errors.append("report does not contain original and derived events")
    if document["conclusion_events"] != 1:
        errors.append("report must contain exactly one conclusion")
    if document["antecedent_ids"] == 0:
        errors.append("report contains no antecedent provenance")
    if not document.get("movement_identity_preserved") or not document.get("deleted_identity_retired"):
        errors.append("movement identity checks are incomplete")
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print(json.dumps({"report": str(args.report), "status": "passed"}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())