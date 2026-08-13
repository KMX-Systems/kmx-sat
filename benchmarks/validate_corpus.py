#!/usr/bin/env python3
"""Validate the pinned DIMACS corpus manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    document = json.loads(args.manifest.read_text(encoding="utf-8"))
    if document.get("schema") != 1 or not isinstance(document.get("instances"), list):
        parser.error("unsupported corpus manifest")

    errors: list[str] = []
    valid_statuses = {"SATISFIABLE", "UNSATISFIABLE"}
    for entry in document["instances"]:
        relative_file = entry.get("file")
        expected_hash = entry.get("sha256")
        expected_status = entry.get("expected_status")
        path = args.manifest.parent / relative_file if isinstance(relative_file, str) else None
        if path is None or not path.is_file():
            errors.append(f"missing corpus file: {relative_file}")
            continue
        if sha256_file(path) != expected_hash:
            errors.append(f"hash mismatch: {relative_file}")
        if expected_status not in valid_statuses:
            errors.append(f"invalid expected status: {relative_file}")

    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print(f"validated {len(document['instances'])} pinned corpus instances")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())