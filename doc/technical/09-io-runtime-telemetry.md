# 9. I/O, Runtime, and Telemetry

*Part of the [KMX SAT Solver technical reference](README.md).*

These three namespaces sit outside the solving core. Their maturity varies more than the core's, so each
subsection states what is on an execution path and what is not.

## 9.1 I/O: `kmx::sat::io`

| Component | Status |
| :--- | :--- |
| `io::dimacs_parser` + `io::file_source` | Implemented, but **exercised only by tests** — the CLI does not use them |
| `io::fixture::binary::{reader,writer}` + `manifest` / `schema` / `validator` | Implemented; the replay-trace fixture format |
| `io::writer::format` | Implemented; formats statistics report lines. Used by `solver::statistics_report_line` |
| `io::proof_output_pipeline`, `io::writer::kmx_aio_proof` | Proof byte-stream output path |

**The CLI parses DIMACS itself.** [kmx-sat-main.cpp](../../source/cli/kmx-sat-main.cpp) contains an inline
`read_dimacs_cnf` with its own 64 KB `fread` buffer and hand-rolled integer scanner, and optionally retains
a copy of the parsed formula for model verification. `io::dimacs_parser` is the library-side equivalent;
the two are separate implementations, so a parser change must be made in both or deliberately in one.

**Fixture serialization has a hard rule.** `io::fixture::binary::reader` may only deserialize stable value
data — literals, clause vectors, assumptions, limits, metadata. It must never deserialize live arena
offsets, watch entries, reason references, or transient trail internals, because none of those survive a
process boundary. Validation failure must roll back cleanly with no partial materialization.

## 9.2 Runtime: `kmx::sat::runtime`

This namespace is **almost entirely scaffolding**. It documents an intended concurrency architecture; it
does not implement one.

| Component | Status |
| :--- | :--- |
| `runtime::controller::signal` | **Real but unwired.** `install_handlers` registers genuine `SIGINT`/`SIGTERM` handlers that only set an async-signal-safe flag. Nothing in `search_coordinator` or the schedulers polls `termination_requested`, despite the header saying they do |
| `runtime::controller::portfolio` | **Scaffolding.** `launch_strategies` sets flags and increments counters; no `solver_core` instance is ever launched |
| `runtime::shared_clause_exchange` | **Scaffolding.** Bookkeeping only |
| `runtime::parallel_preprocess_executor` | **Explicitly not implemented** (`@warning` in the header): `run_parallel_pass` computes a chunk size and nothing runs in parallel |
| `runtime::co_fsm_adapter` | **Deliberate exclusion marker.** An empty placeholder recording the decision *not* to build coroutine-based FSM orchestration, so the reasoning does not have to be rediscovered |

**Practical consequence: the solver is single-threaded and cancellation is caller-driven.** Use
`solver::set_terminate` for timeouts; a `Ctrl-C` will not produce an orderly stop unless the embedding
application installs its own handler and drives that callback.

## 9.3 Telemetry: `kmx::sat::telemetry`

| Component | Role |
| :--- | :--- |
| `telemetry::solver_statistics` | The counter set and its immutable `snapshot` |
| `telemetry::ema_tracker` | Exponential moving averages (glue fast/slow, inprocessing pressure signals) |
| `telemetry::profile_clock` | Wall and process time for the verbose report, bracketed per phase (`start_phase` / `stop_phase`) |
| `telemetry::phase_id` | The closed set of separately timed phases: `parse`, `preprocess`, `search`, `inprocess`, `proof_check` — `profile_clock` and `logging_facade` label against the same identity |
| `telemetry::report_formatter` | Renders a snapshot to a line |
| `telemetry::logging_facade` | Log sink indirection |

`solve_result` carries a `snapshot` for every outcome. The compact report line is:

```
conflicts=N decisions=N propagations=N restarts=N learned_clauses=N
learned_clause_glue_total=N learned_clause_glue_samples=N
reduction_passes=N reduced_clauses=N deleted_clauses=N
```

Verbose (`set_option(option_id::statistics_verbose_reporting, 1)`) appends timing and callback counters:
`time=<ms> cpu=<ms>`, `terminate_callback_calls`, `learn_callback_calls`, `external_propagator_calls`,
`option_updates`, `configuration_updates`.

Average learned glue is deliberately *not* a stored field — divide `learned_clause_glue_total` by
`learned_clause_glue_samples`, which keeps the counter exact and lets a consumer window it.

The facade retains the last 64 emitted report lines and their snapshots, so
`emitted_statistics_snapshot_tail_delta()` gives the movement between the last two checkpoints without the
caller having to track state. `emitted_statistics_snapshot_tail_monotonic()` reports whether the retained
tail is monotonic — a cheap invariant check for a long incremental run.

The CLI prints its own line on `stdout` as a DIMACS comment before the status line:

```
c kmx-stats conflicts=… decisions=… … proof_events=… proof_buffered_payload_bytes=…
```

---

[← 8. Proof Pipeline and Verification](08-proof-pipeline.md) · [Index](README.md) · [10. Configuration Reference →](10-configuration.md)
