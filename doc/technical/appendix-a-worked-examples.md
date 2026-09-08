# Appendix A. Worked Examples

*Part of the [KMX SAT Solver technical reference](README.md).*

## A.1 Minimal C++ use

```cpp
#include <kmx/sat/solver.hpp>
#include <print>

using namespace kmx::sat;

int main()
{
    solver s;
    s.reserve(3);                                  // optional; sizes internal vectors up front

    const auto lit = [](int v) {                   // DIMACS-style literal helper
        return literal {variable {static_cast<variable::index_t>(std::abs(v))}, v < 0};
    };

    const literal c1[] {lit(1), lit(-2)};          // (x1 or not x2)
    const literal c2[] {lit(2), lit(3)};           // (x2 or x3)
    const literal c3[] {lit(-1), lit(-3)};         // (not x1 or not x3)
    s.add_clause(c1);
    s.add_clause(c2);
    s.add_clause(c3);

    solve_request request;
    request.conflict_limit = 0;                    // unlimited

    const auto result = s.solve(request);
    switch (result.status_of())
    {
        case solve_result::status::satisfiable:
            for (variable::index_t v = 1; v <= 3; ++v)
                if (const auto value = s.value_of(variable {v}); value.has_value())
                    std::println("x{} = {}", v, *value);
            break;
        case solve_result::status::unsatisfiable:
            std::println("UNSAT");
            break;
        default:
            std::println("UNKNOWN");
            break;
    }

    const auto stats = result.statistics_snapshot();   // solver::statistics() gives the same on the facade
    std::println("conflicts={} decisions={}", stats.conflicts, stats.decisions);
}
```

Link against `kmx-sat-lib`; include paths are `source/library/api` and `source/library/inc`.

## A.2 Incremental episodes with assumptions

```cpp
solver s;
/* … add the permanent clause database … */

for (const auto candidate: candidates)
{
    s.assume(candidate);                        // scoped to the next solve only
    const auto result = s.solve(solve_request {});

    if (result.status_of() == solve_result::status::unsatisfiable)
    {
        // Which assumptions were responsible?
        for (const auto a: active_assumptions)
            if (s.failed(a))
                record_conflicting(a);
    }
    s.release_incremental_assumptions();
    s.add_clause(refinement_for(candidate));    // the database grows across episodes
}
```

Two rules for this pattern: leave BVE off (the default) — see
[§7.6](07-incremental-solving.md#76-incremental-caveats) — and re-verify models yourself, because the library ships no witness
check ([§6.6](06-simplification.md#66-model-reconstruction-and-witness-verification)).

## A.3 CLI

```console
$ kmx-sat problem.cnf
c kmx-stats conflicts=4989 decisions=5401 propagations=1873220 restarts=3 \
learned_clauses=4989 learned_clause_glue_total=41209 learned_clause_glue_samples=4989 \
reduction_passes=1 reduced_clauses=1204 deleted_clauses=1204 proof_events=0 \
proof_buffered_payload_bytes=0
s SATISFIABLE
v 1 -2 3 -4 … 0
$ echo $?
10
```

(The `c kmx-stats` values above are illustrative of the format, not a recorded run.)

Useful invocations:

```bash
kmx-sat problem.cnf --no-model                       # status line only
kmx-sat problem.cnf --conflict-limit 100000          # bounded effort; exit 0 on UNKNOWN
kmx-sat problem.cnf --assume 3 --assume -7           # solve under assumptions
kmx-sat problem.cnf --enabled-pass-mask 0            # disable all simplification
kmx-sat problem.cnf --enabled-pass-mask 8191         # baseline + BVE (non-incremental only)
kmx-sat problem.cnf --restart-interval 8192 --reduction-interval 1000
kmx-sat problem.cnf --local-search-effort-percent 0   # CDCL only, for measuring the core
```

---

[← 13. Implementation Decisions Matrix](13-implementation-decisions.md) · [Index](README.md) · [Appendix B. Glossary →](appendix-b-glossary.md)
