#!/usr/bin/env python3
"""Generate a reproducible public IPASIR API and ABI compatibility report."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
EXPECTED_SYMBOLS = (
    "ipasir_add",
    "ipasir_assume",
    "ipasir_failed",
    "ipasir_init",
    "ipasir_release",
    "ipasir_solve",
    "ipasir_val",
)


def run(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd or ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)


def compile_public_header(header: Path) -> dict[str, object]:
    include_root = header.parents[2]
    with tempfile.TemporaryDirectory(prefix="kmx-sat-api-abi-") as temporary_directory:
        temporary = Path(temporary_directory)
        source = temporary / "header_smoke.c"
        object_file = temporary / "header_smoke.o"
        source.write_text("#include <kmx/sat/ipasir.h>\nint main(void) { return 0; }\n", encoding="ascii")
        compiler = os.environ.get("CC", "gcc")
        result = run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(include_root), "-c", str(source), "-o", str(object_file)]
        )
        return {
            "compiler": compiler,
            "passed": result.returncode == 0,
            "output": result.stdout[-4096:],
        }


def exported_symbols(library: Path) -> tuple[set[str], str]:
    result = run(["nm", "-g", "--defined-only", str(library)])
    symbols = {line.split()[-1] for line in result.stdout.splitlines() if line.split() and line.split()[-1] in EXPECTED_SYMBOLS}
    return symbols, result.stdout[-4096:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--header", type=Path, default=Path("source/library/api/kmx/sat/ipasir.h"))
    parser.add_argument("--output", type=Path, default=Path("api-abi-report.json"))
    args = parser.parse_args()

    header = args.header if args.header.is_absolute() else ROOT / args.header
    library = args.library if args.library.is_absolute() else ROOT / args.library
    header_result = compile_public_header(header)
    symbols, nm_output = exported_symbols(library)
    report = {
        "schema": 1,
        "header": str(header.relative_to(ROOT)) if header.is_relative_to(ROOT) else str(header),
        "library": str(library.relative_to(ROOT)) if library.is_relative_to(ROOT) else str(library),
        "header_compile": header_result,
        "expected_symbols": list(EXPECTED_SYMBOLS),
        "exported_symbols": sorted(symbols),
        "missing_symbols": sorted(set(EXPECTED_SYMBOLS) - symbols),
        "nm_output_tail": nm_output,
        "status": "passed" if header_result["passed"] and symbols == set(EXPECTED_SYMBOLS) else "failed",
    }
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "status": report["status"]}, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
