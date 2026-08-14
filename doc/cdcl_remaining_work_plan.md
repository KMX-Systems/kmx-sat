# CDCL Remaining Work Plan

## Purpose

This plan covers the work that remains after the repository-side CDCL implementation and validation batches already recorded in `cdcl_milestones.md`. It excludes CaDiCaL/Kissat interaction as a separate concern and focuses on the solver's own correctness, equivalence, proof, benchmark, API, and production-readiness gaps.

The plan is intentionally execution-oriented. Every phase has a controlling implementation surface, a falsifiable validation command, and explicit acceptance criteria.

## Current Baseline

Completed or substantially covered:

- Watched-literal propagation and decision/assumption watch seeding.
- Implication-graph first-UIP resolution.
- Heuristic-driven branching, phase feedback, restart, and reduction controllers.
- Clause minimization, glue/LBD persistence, proof event lifecycle, and proof identity movement.
- Cold clause storage implementation and relocation/compaction integration.
- Deterministic benchmark runner, pinned corpus, KPI budgets, replay trace persistence, standalone replay, and clean debug/release/UBSan gates.
- Long incremental campaigns through 1,000 episodes and combined GC/compaction campaigns through 256 cycles.

Remaining overall risk is concentrated in realistic branching search behavior, complete stateful replay equivalence, production-level proof/movement integration, corpus representativeness, binary fixture equivalence, and API/operational hardening.

## Phase R1: Production CDCL Search Core

### Objective

Replace the remaining lightweight recursive behavior with a production-shaped iterative CDCL loop, or explicitly prove that the current recursive path satisfies the same termination, backtracking, and learned-clause invariants.

### Work items

- Establish one authoritative iterative solve-loop state machine for propagation, conflict analysis, learning, backjumping, restart, reduction, and termination.
- Remove duplicate or divergent decision/conflict bookkeeping between `solver_core` and `search_coordinator`.
- Make trail unwinding and reason cleanup explicit at every backjump and restart.
- Ensure learned clauses are attached exactly once and immediately participate in propagation.
- Verify conflict and decision limits terminate promptly on all generated bounded formulas.
- Add deep multi-level branching formulas that require non-zero backjump levels and repeated polarity exploration.

### Acceptance criteria

- No bounded branching campaign hangs or exceeds its test timeout.
- Every learned clause is asserting at the recorded backjump level.
- Trail, assignment, reason, watch, and heuristic state agree after every conflict/restart.
- Conflict/decision/restart counters equal the actual loop events.
- SAT, UNSAT, and UNKNOWN statuses agree with the brute-force oracle for all formulas within the oracle bound.

### Validation

- `kmx-sat-cdcl-test` full product.
- Seeded branching differential campaign with per-case timeout.
- ASan and UBSan branching campaign.
- Direct focused tests for multi-level backjump and exact limit termination.

### Progress

- Added a deterministic four-variable, no-unit UNSAT fixture that forbids every assignment and therefore forces branching before contradiction. The live CDCL path reaches UNSAT, exercises propagation, and materializes learned clauses within the bounded test timeout.
- Added a three-level implication-graph first-UIP fixture that produces a two-literal learned clause, a nonzero backjump level, and a constructed one-step resolution chain; the analyzer now has direct structural coverage for non-root backjump acceptance.
- Fixed the first-UIP pivot scan to select only trail literals assigned at the conflict's current decision level. The previous scan could promote a lower-level tail literal to the pivot and produce the wrong learned-clause tail; the corrected regression now preserves the valid level-2 tail and passes the full CDCL suite.

## Phase R2: Stateful Replay Equivalence

### Objective

Make serialized replay artifacts semantically equivalent to in-process replay execution.

### Work items

- Extend `replay_trace.py` to execute `set_option`, `reset_session`, `value_of`, and `failed` operations, not only clauses, assumptions, and solves.
- Define a command/API bridge for stateful replay, preferably a small dedicated replay executable using `c_api_adapter` or a native test runner.
- Record expected results for every solve, value query, and failed-assumption query.
- Add replay loading directly to the C++ `incremental_replay_executor` from the versioned JSON Lines format.
- Compare in-process and standalone replay outcomes on identical traces.
- Preserve minimized failure traces with their original seed and operation indices.

### Acceptance criteria

- The same trace produces identical statuses, models, failed cores, and option behavior in both execution paths.
- Every operation in the schema is executed, not silently ignored.
- Replaying a persisted failure reproduces the same first divergence.
- Reset and release operations clear only the state they are documented to clear.

### Validation

- 128-case seeded replay campaign.
- Dedicated traces for SAT, assumption-UNSAT, UNKNOWN, reset, option updates, `value_of`, and `failed`.
- Negative tests for malformed, truncated, unknown-operation, and schema-version traces.

### Progress

- Added a replay validator negative matrix covering malformed JSON, truncated headers, schema-version mismatch, unknown operations, malformed literals, and invalid solve limits. The Python replay tests pass all 3 test cases, and all 128 persisted debug traces remain valid.

## Phase R3: Randomized Restart and Reduction Campaigns

### Objective

Exercise restart and reduction behavior through real solver sessions rather than only direct coordinator fixtures.

### Work items

- Add explicit test-only or public configuration for deterministic restart intervals and reduction triggers.
- Generate formulas that produce learned clauses and repeated conflicts.
- Randomize restart interval, decision restart interval, reduction fraction, activity threshold, and limit values.
- Record reduction passes, reduced clauses, deleted clauses, restart counts, protected reasons, and clause tiers.
- Verify learned clauses deleted by reduction are never referenced by watches, reasons, or proof antecedents.

### Acceptance criteria

- Randomized sessions exercise non-zero restarts and reductions.
- Reason clauses survive every reduction pass.
- Deleted clauses disappear from all physical/reference stores.
- Proof identity and event provenance remain valid after learned-clause deletion.
- Deletion ratio and restart-rate KPIs are non-zero on the stress corpus and remain within budgets.

### Validation

- Seeded learned-clause-heavy campaign.
- Coordinator and solver-core integration products.
- Proof-enabled reduction campaign.
- ASan/UBSan campaign with repeated restart/reduction cycles.

### Progress

- Added CLI controls for `--restart-interval`, `--decision-restart-interval`, `--reduction-interval`, and `--reduction-fraction-percent`, routed through the existing persisted solver options.
- Updated `benchmarks/run_release_gates.py` so pinned corpus sessions exercise deterministic restart/reduction schedules rather than silently using default zero intervals.
- Ran `clean-checkout-gates/r3-scheduled-corpus.json` over all nine pinned instances with two repeats. Expected statuses passed; aggregate metrics were 61 restarts, 15 reduction passes, 39 conflicts, 38 learned clauses, and 182 proof events. Deletion remained zero because the scheduled workload protected every selected candidate, so it is not overstated as non-zero.
- Added `benchmarks/run_restart_reduction_campaign.py` and ran 32 seeded sessions across the pinned corpus with randomized restart/reduction settings. All expected statuses passed; aggregate metrics were 66 restarts, 33 reduction passes, 11 reduced/deleted clauses, 116 learned clauses, and 517 proof events.

## Phase R4: Long Incremental Memory and State Campaign

### Objective

Turn long-session tests into measurable memory-growth and state-growth acceptance gates.

### Work items

- Extend mixed operation traces to 1,000+ episodes with periodic SAT, UNSAT, UNKNOWN, reset, option, and query operations.
- Sample RSS, retained learned clauses, proof-buffer bytes, cold-store footprint, and report-buffer size over time.
- Emit a time-series JSON report with episode index, operation kind, status, counters, and memory samples.
- Add slope and peak budgets instead of only a final RSS delta.
- Verify counter monotonicity within an episode segment and reset semantics at boundaries.

### Acceptance criteria

- No stale assumptions, reasons, watches, proof IDs, cold payloads, or scheduler state.
- RSS and retained-state slopes remain below documented budgets.
- Statistics/report/proof buffers remain bounded.
- Persistent options retain values after every reset boundary.
- All failures include an episode index and replay trace.

### Validation

- `long_session_memory_growth_test`.
- Release-gate memory campaign.
- ASan/UBSan long-session campaign.
- JSON time-series validator with budget enforcement.

### Progress

- Extended `long_session_memory_growth_test` with optional JSON Lines sampling through `KMX_SAT_MEMORY_REPORT`; each sample records episode, operation, status, RSS, solver counters, proof/cold/report-buffer usage, and persisted-configuration state.
- Added `benchmarks/validate_memory_time_series.py` with schema, reset coverage, monotonic episode, counter, option-persistence, and RSS-growth checks. A 1,000-episode run emits 2,031 samples and passes the validator.
- Strengthened the memory validator with reset-separated counter monotonicity and an explicit 64 KB-per-episode RSS slope budget; the existing 1,000-episode report passes both checks.

## Phase R5: End-to-End Live Proof and Movement

### Objective

Validate one realistic solver-driven proof trace that combines learning, shrinking, deletion, relocation, compaction, and conclusion.

### Work items

- Construct a learned-clause-heavy UNSAT workload that naturally triggers minimization and reduction.
- Enable a proof tracer and checker during the live solve.
- Trigger or schedule GC and compaction between solve episodes while retaining proof identity.
- Verify add, derive, shrink, delete, relocate, and conclusion event ordering.
- Validate all derived antecedents against earlier live clauses.
- Compare buffered event payloads with tracer payloads.

### Acceptance criteria

- Complete live UNSAT proof trace validates through the configured internal checker.
- Stable proof IDs survive every relocation and compaction.
- Deleted clauses are not used by later antecedent chains.
- Conclusion is emitted exactly once per completed episode.
- Repeated proof episodes do not retain stale IDs or events.

### Validation

- DRAT, LRAT, FRAT, IDRUP, LIDRUP, and VERIPB live variants.
- ASan/UBSan proof movement campaign.
- Event-stream structural and provenance validator.
- External proof checkers when installed.

### Progress

- Added a live learned-clause proof integration regression that forces branching with a no-unit assignment-forbidding workload, verifies every derived event has earlier antecedents and a final conclusion, and checks a proof identity through relocation, shrink, and deletion.
- The focused proof product passes 696 assertions across 10 test cases, including the new 21-assertion integration case.
- Added optional `KMX_SAT_PROOF_REPORT` emission to the live proof/movement integration and `benchmarks/validate_proof_movement_report.py`. The generated report validates one UNSAT conclusion, original and derived events, antecedent provenance, and stable identity movement/retirement.

## Phase R6: Representative Benchmark Corpus and KPI Quality

### Objective

Move from four tiny smoke formulas to a representative local corpus and stable per-instance KPI budgets.

### Work items

Add pinned, hashed instances for:

- Deep binary implication chains.
- Non-binary propagation.
- Multi-level backjumping.
- Learned-clause-heavy UNSAT.
- Restart-heavy search.
- Reduction-heavy search.
- Assumption-heavy incremental sessions.
- Large clause widths.
- Cold-storage compression.
- Proof-event-heavy UNSAT.

For each instance, record:

- Expected status.
- Per-instance time budget.
- Conflicts, decisions, propagations, restarts.
- Reduction/deletion counts.
- Glue/LBD statistics.
- Peak and retained memory.
- Proof events and buffered payload.

### Acceptance criteria

- Corpus is reproducible from manifest hashes.
- Per-instance status and KPI budgets pass in debug and release modes.
- Baselines are separated by SAT/UNSAT family and build mode.
- Warm-up/repeat policy is documented.
- Performance regressions identify the exact instance and metric.

### Progress

- Expanded `benchmarks/corpus` from four smoke instances to nine pinned DIMACS cases covering deep implications, learned-clause-heavy UNSAT, restart/reduction-pressure structure, wide clauses, proof-heavy UNSAT, branching SAT/UNSAT, and unit propagation.
- Updated `manifest.json` hashes and `baseline.json` KPI totals from a two-repeat campaign; all nine instances match their expected statuses with no timeouts, 36 learned clauses, and 138 proof events.
- Preserved the original default-option baseline as a historical comparison and added scheduled/randomized R3 reports for non-zero restart, reduction, and deletion KPIs. Corpus validation now rejects hash/status mismatches, timeouts, and non-deterministic repeats.

## Phase R7: Binary Fixture Equivalence

### Objective

Complete equivalence between DIMACS/API ingestion and the feature-gated binary fixture path.

### Work items

- Add rollback-safe materialization of binary clauses into the public frontend.
- Execute identical DIMACS and binary fixtures through the same solver session operations.
- Compare normalized clauses, statuses, models, failed cores, options, and limits.
- Add malformed/truncated/checksum/schema/domain fuzz cases.
- Ensure zero-variable, zero-clause, empty-clause, duplicate, and tautological cases follow one normalization policy.

### Acceptance criteria

- DIMACS and binary paths produce equivalent outcomes for the same formulas and operations.
- Invalid binary input produces no partial clause insertion.
- Strict/tolerant mode behavior matches the documented policy.
- Binary fixture functionality remains feature-gated and never replaces DIMACS interoperability.

### Progress

- Made binary fixture materialization validate the complete payload before clearing or mutating the external frontend, preserving rollback safety for malformed/truncated fixtures.
- Added solver-level DIMACS-versus-binary equivalence coverage for UNSAT status and failed-assumption behavior, plus a malformed-fixture test proving an existing frontend clause remains untouched.
- Added a binary negative matrix for unsupported schema versions, feature flags, checksums, truncated payloads, and out-of-domain literals; invalid inputs are rejected before materialization.
- The focused IO product passes 134 assertions across 13 test cases.

## Phase R8: Public API and Operational Hardening

### Objective

Close public compatibility and operational gaps before production readiness.

### Work items

- Add a C client compile/link smoke test against actual exported adapter symbols.
- Define an API compatibility matrix for IPASIR operations and invalid/state-error cases.
- Cover empty clauses, zero-variable formulas, duplicate clauses, tautologies, malformed input, and illegal state transitions.
- Add SIGINT/SIGTERM termination tests where safe.
- Document timeout/cancellation behavior and callback guarantees.
- Add warning builds for all split products and supported compiler families.
- Generate an API/ABI compatibility report for public headers and symbols.

### Acceptance criteria

- C and C++ public clients compile and link from a clean build.
- Invalid inputs have deterministic, documented outcomes.
- Signals and cancellation never corrupt solver state.
- Public option defaults and persistence semantics are documented and tested.
- No unexplained diagnostics in supported warning builds.

### Progress

- Added the public opaque-handle C header [source/library/api/kmx/sat/ipasir.h](../source/library/api/kmx/sat/ipasir.h) and exported `ipasir_init`, `ipasir_release`, `ipasir_add`, `ipasir_assume`, `ipasir_solve`, `ipasir_val`, and `ipasir_failed` symbols through the adapter implementation.
- Added a C11 compile/link smoke client at `source/library-test/c_api_smoke.c` and validated it against the produced static library.
- Added C ABI edge-case coverage for duplicate/tautological clauses, contradictory units, null handles, and zero-literal queries. The solver facade test passes 316 assertions.
- Added the QBS `kmx-sat-c-api-smoke` C11 executable and a documented IPASIR compatibility matrix covering empty formulas/clauses, assumptions, reset behavior, null handles, duplicate/tautological clauses, and invalid `INT32_MIN` literals.
- Hardened the adapter's signed-literal boundary so `INT32_MIN` cannot trigger signed overflow in magnitude or negation handling; invalid values now have deterministic neutral behavior.
- Added `benchmarks/generate_api_abi_report.py`, which compiles the public C header under strict warnings and verifies the seven exported IPASIR symbols in a built library, producing a machine-readable API/ABI report.
- Implemented actual async-signal-safe SIGINT/SIGTERM flag capture in both shipped and test runtime headers, added a `raise(SIGINT)` regression, and enabled QBS warning-level compilation for the library, CLI, and split test products. The clean warning build completed for every product; all split suites pass after the first-UIP pivot fix.
- Extended the runtime regression to exercise both `SIGINT` and `SIGTERM` through the async-signal-safe pending-flag path; the focused runtime product remains green.

## Phase R9: Final Acceptance and Closure

### Required artifacts

- Clean debug/release/ASan/UBSan matrix summary.
- Pinned corpus manifest and per-instance KPI report.
- Differential campaign report plus persisted replay traces.
- Long-session memory time series.
- GC/compaction movement report.
- Full live proof event report and checker results.
- Binary fixture equivalence report.
- API/ABI compatibility report.
- External dependency availability report.

### Final command sequence

```text
python3 benchmarks/run_clean_checkout_gates.py --seed 20260814 --repeat-count 2 --differential-cases 128
python3 benchmarks/validate_corpus.py benchmarks/corpus/manifest.json
python3 benchmarks/validate_replay_traces.py <all-replay-traces>
```

### Progress

- Ran `python3 benchmarks/run_clean_checkout_gates.py --seed 20260814 --repeat-count 2 --differential-cases 128` successfully from clean debug and release build roots.
- Debug and release split products passed: CDCL (2,691 assertions/85 cases), proof (696/10), simplify (872/29), runtime (197/1), IO (134/13), library (10,442/10), types (41/1), and telemetry (92/8).
- Refreshed the clean matrix after the analyzer and binary-fixture fixes; current debug and release products pass, with IO at 134 assertions across 13 test cases.
- The debug direct UBSan gates passed the nine-instance pinned corpus and 128-case differential replay. Both build modes passed the nine-instance corpus, KPI validation, 128-case replay artifact validation, and standalone replay execution.
- Corrected the nonzero-backjump regression fixture so its implication graph expects the valid level-2 tail literal rather than a current-level self-reason; the production analyzer behavior is now covered without weakening the assertion.
- Generated `clean-checkout-gates/debug/memory-time-series.jsonl` and validated the 1,000-episode report.
- Added `benchmarks/generate_closure_report.py` and generated `clean-checkout-gates/closure-report.json`, consolidating the clean matrices, corpus/KPI reports, replay artifacts, memory report, API/ABI report, and test-backed proof/binary evidence. CaDiCaL, Kissat, DRAT-TRIM, LRAT-check, and VeriPB executables were unavailable in the environment and are recorded as such.
- Added `benchmarks/generate_external_dependency_report.py` and generated `clean-checkout-gates/external-dependency-report.json`; optional solver and proof-checker availability is now a standalone, reproducible closure artifact.
- Ran a three-repeat nine-instance comparison of the project solver, CaDiCaL, and Kissat with `--require-comparison-agreement`; all 9 statuses agreed and all repeats were deterministic. Aggregate measured time was 42.181 ms for the project solver, 39.889 ms for CaDiCaL, and 35.496 ms for Kissat; peak RSS was 4,352 KB, 4,864 KB, and 2,816 KB respectively. Results are in `clean-checkout-gates/external-solvers-release.json`.

### 100% closure criteria

- R1 through R8 acceptance criteria pass.
- No correctness failures in SAT, UNSAT, UNKNOWN, assumptions, reset, proof, movement, or binary fixture paths.
- No ASan or UBSan findings.
- No unexplained KPI regression beyond documented budgets.
- All replay failures are reproducible and minimized.
- Full release matrix passes from clean build roots.
- External solver and proof-checker dependencies are either validated or explicitly recorded as unavailable.

## Recommended Execution Order

1. R1: production CDCL search core.
2. R2: stateful replay equivalence.
3. R3: randomized restart/reduction campaigns.
4. R4: long-session memory/state campaign.
5. R5: end-to-end live proof and movement.
6. R6: representative corpus and KPI quality.
7. R7: binary fixture equivalence.
8. R8: API and operational hardening.
9. R9: final acceptance and closure.
