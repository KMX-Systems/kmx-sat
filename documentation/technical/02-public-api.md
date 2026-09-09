# 2. Public API and External Integration

*Part of the [KMX SAT Solver technical reference](README.md).*

## 2.1 C++ facade: `kmx::sat::solver`

The primary consumer interface is [solver.hpp](../../source/library/api/kmx/sat/solver.hpp). It owns a
pimpl and exposes:

| Group | Members |
| :--- | :--- |
| Problem input | `reserve(variable_count)`, `add_literal(lit)`, `add_clause(span<const literal>)` |
| Assumptions | `assume(lit)`, `release_incremental_assumptions()` |
| Search | `solve(const solve_request&) -> solve_result` |
| Query | `value_of(var) -> optional<bool>`, `failed(lit) -> bool` |
| Configuration | `set_option(option_id, int64)`, `set_configuration(configuration_profile_id)`, `clear_persisted_configuration()` |
| Proof | `attach_proof_sink(proof::tracer::view&)`, `proof_buffered_event_count()`, `buffered_proof_events()` |
| Callbacks | `set_terminate(fn)`, `set_learn(fn)`, `set_external_propagator(fn)` |
| Telemetry | `statistics_report_line()`, `last_emitted_statistics_snapshot()`, `emitted_statistics_snapshot_tail_delta()` |
| Lifecycle | `reset_session()` |

`solve` is the only non-`noexcept` member of the input path; everything else is `noexcept` and reports
failure through return values.

## 2.2 `solve_request` and `solve_result`

[solve_request](../../source/library/api/kmx/sat/solve_request.hpp) is read-only for the duration of an episode,
so assumptions, limits, and pass selection cannot change mid-search:

| Field | Meaning |
| :--- | :--- |
| `assumptions` | Literals assumed for this episode; kept strictly separate from the permanent clause database |
| `conflict_limit` | Conflicts before the episode reports `unknown`; `0` means unlimited |
| `decision_limit` | Decisions before the episode reports `unknown`; `0` means unlimited |
| `enabled_pass_mask` | Bitmask over `simplify::pass_id` selecting permitted passes; `0` leaves the scheduler default in place. See [§10.3](10-configuration.md#103-simplification-pass-mask) |
| `strict_mode` | Reject malformed or out-of-domain input immediately rather than tolerating benign metadata irregularities |

[solve_result](../../source/library/api/kmx/sat/solve_result.hpp) is a single return type covering every terminal
outcome, so callers never branch on separate SAT/UNSAT types:

| `status` | Meaning |
| :--- | :--- |
| `satisfiable` | `model()` is populated |
| `unsatisfiable` | `failed_core()` is populated when assumptions were active |
| `unknown` | Conflict/decision limit exhausted, or the terminate callback returned true |
| `terminated` | Stopped by the runtime itself, e.g. an unrecoverable `memory_governor` ceiling |

Every outcome also carries a `telemetry::solver_statistics::snapshot` and a `proof_summary`
(`proof_enabled`, `proof_checked`).

## 2.3 IPASIR C ABI

[ipasir.h](../../source/library/api/kmx/sat/ipasir.h) and
[c_api_adapter.cpp](../../source/library/src/kmx/sat/c_api_adapter.cpp) export a **seven-function subset** of
IPASIR:

| Operation | Return | Semantics and state effect |
| :--- | :--- | :--- |
| `ipasir_init()` | non-null handle | Fresh solver session; no state carried over from prior handles |
| `ipasir_release(h)` | `void` | Deallocates the session. `NULL` is a safe no-op |
| `ipasir_add(h, lit)` | `void` | Ingests a signed DIMACS literal; `0` terminates the clause. `INT32_MIN` is ignored |
| `ipasir_assume(h, lit)` | `void` | Assumption for the next `solve`. `0` and `INT32_MIN` are ignored |
| `ipasir_solve(h)` | `10` SAT / `20` UNSAT / `0` unknown | Runs one episode with a default `solve_request`. `NULL` returns `0` |
| `ipasir_val(h, lit)` | signed literal, or `0` | Value after SAT; `0` if unassigned or the last result was not SAT |
| `ipasir_failed(h, lit)` | `1` / `0` | Whether the assumption is in the UNSAT core |

> **Not exported.** `ipasir_signature`, `ipasir_set_terminate`, `ipasir_set_learn`, and the whole
> IPASIR-UP (user-propagator) surface are absent from the C ABI. A harness expecting stock IPASIR will
> fail to link against the missing three. Termination and learned-clause callbacks are reachable only
> from C++ via `solver::set_terminate` and `solver::set_learn`. `solver::set_external_propagator` takes a
> `std::function<void()>` notification hook — it is not an IPASIR-UP propagator interface.

Because `ipasir_solve` constructs a default `solve_request`, C clients get the scheduler's default pass
set and no limits; per-episode tuning requires the C++ facade.

### Cancellation and cooperative safe points

The C ABI has no timeout argument. C++ applications register a non-blocking callback through
`solver::set_terminate()`. The engine polls it at deterministic safe points — the top of the BCP loop and
before branch decisions — and a `true` return ends the search with `status::unknown` (IPASIR `0`).

---

[← 1. Overview](01-overview.md) · [Index](README.md) · [3. Memory Architecture and Physical Storage →](03-memory-architecture.md)
