# SAT Solver Performance Benchmarks

The repository contains command-driven benchmark scripts for comparing the project
solver with external SAT solvers. The comparison tools installed for this workspace
are CaDiCaL and Kissat. All commands below run from the repository root.

## Enable Local Tools

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
export PATH="$PWD/tools/bin:$PATH"
```

This makes the following commands available:

```text
cadical
kissat
drat-trim
lrat-check
veripb
```

## Compare the Three SAT Solvers

The three solver comparison is:

1. The project solver: `source/build/clean-gate-release/default/kmx-sat.d9e8dc1a/kmx-sat`
2. CaDiCaL: `tools/bin/cadical`
3. Kissat: `tools/bin/kissat`

Run the pinned nine-instance corpus with three repeats:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
export PATH="$PWD/tools/bin:$PATH"

python3 benchmarks/run_benchmarks.py benchmarks/corpus/*.cnf \
  --manifest benchmarks/corpus/manifest.json \
  --command "$PWD/source/build/clean-gate-release/default/kmx-sat.d9e8dc1a/kmx-sat {instance}" \
  --compare-command "cadical=$PWD/tools/bin/cadical {instance}" \
  --compare-command "kissat=$PWD/tools/bin/kissat {instance}" \
  --require-comparison-agreement \
  --repeat-count 3 \
  --seed 20260814 \
  --configuration external-solvers-release \
  --output clean-checkout-gates/external-solvers-release.json
```

`run_benchmarks.py` records, per instance and solver:

- SAT/UNSAT/UNKNOWN status.
- Exit code.
- Elapsed time.
- Peak RSS when `/usr/bin/time` is available.
- Solver statistics parsed from output.
- Repeat determinism.
- Comparison status agreement.

The report is written to:

```text
clean-checkout-gates/external-solvers-release.json
```

The recorded comparison from 2026-08-14 is available at
[clean-checkout-gates/external-solvers-release.json](../clean-checkout-gates/external-solvers-release.json).

## Scheduled Restart and Reduction Benchmark

The project CLI accepts deterministic scheduling controls:

```text
--restart-interval <n>
--decision-restart-interval <n>
--reduction-interval <n>
--reduction-fraction-percent <n>
```

Run the seeded randomized campaign:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
python3 benchmarks/run_restart_reduction_campaign.py \
  --solver "$PWD/source/build/clean-gate-release/default/kmx-sat.d9e8dc1a/kmx-sat" \
  --cases 32 \
  --seed 20260814 \
  --output clean-checkout-gates/r3-randomized-campaign.json
```

This records real-session conflicts, decisions, restarts, learned clauses,
reduction passes, reduced/deleted clauses, and proof events.

## Corpus and Report Validation

Validate the pinned corpus manifest:

```bash
python3 benchmarks/validate_corpus.py benchmarks/corpus/manifest.json
```

Validate a benchmark result against its KPI baseline:

```bash
python3 benchmarks/validate_results.py \
  clean-checkout-gates/release/pinned-corpus.json \
  --baseline benchmarks/corpus/baseline.json
```

The validator rejects:

- Missing or mismatched instance hashes.
- Unexpected SAT/UNSAT statuses.
- Timeouts.
- Non-deterministic repeats.
- Missing benchmark metrics.
- KPI regressions outside the configured budget.

## Differential and Replay Campaigns

Run the full debug/release/UBSan acceptance matrix:

```bash
python3 benchmarks/run_clean_checkout_gates.py \
  --seed 20260814 \
  --repeat-count 2 \
  --differential-cases 128
```

This builds clean debug and release roots and runs:

- Pinned corpus and KPI gates.
- 128 randomized differential cases.
- Replay artifact validation.
- Standalone replay execution.
- All split test products.
- Direct UBSan corpus and differential gates for the debug variant.

Validate persisted replay traces directly:

```bash
python3 benchmarks/validate_replay_traces.py \
  clean-checkout-gates/debug/replays/case-*.jsonl
```

Run replay unit tests, including malformed-trace cases:

```bash
cd benchmarks
python3 -m unittest -v test_replay_trace.py
```

## Memory, Proof, API, and Closure Reports

Generate and validate the 1,000-episode memory report:

```bash
export KMX_SAT_MEMORY_REPORT="$PWD/clean-checkout-gates/debug/memory-time-series.jsonl"
export ASAN_OPTIONS=detect_leaks=1:leak_check_at_exit=1

source/build/clean-gate-debug/default/kmx-sat-lib-test.d24ad72f/kmx-sat-lib-test \
  "long incremental session keeps memory and telemetry bounded" \
  --reporter compact

python3 benchmarks/validate_memory_time_series.py \
  "$KMX_SAT_MEMORY_REPORT"
```

Generate the live proof movement report:

```bash
export KMX_SAT_PROOF_REPORT="$PWD/clean-checkout-gates/debug/proof-movement-report.json"
source/build/clean-gate-debug/default/kmx-sat-proof-test.45677cd8/kmx-sat-proof-test \
  --reporter compact
python3 benchmarks/validate_proof_movement_report.py "$KMX_SAT_PROOF_REPORT"
```

Generate the API/ABI report:

```bash
python3 benchmarks/generate_api_abi_report.py \
  --library source/build/clean-gate-release/default/kmx-sat-lib.db0b78d3/libkmx-sat-lib.a \
  --output clean-checkout-gates/api-abi-report.json
```

Consolidate all closure artifacts:

```bash
PATH="$PWD/tools/bin:$PATH" \
  python3 benchmarks/generate_closure_report.py \
  --output clean-checkout-gates/closure-report.json
```

The consolidated report covers clean matrices, corpus/KPI reports, differential and
replay artifacts, memory, proof movement, API/ABI, external dependencies, and the
external solver comparison.
