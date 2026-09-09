# KMX SAT Solver

KMX SAT (`kmx-sat`) is a conflict-driven clause-learning (CDCL) Boolean satisfiability solver written in
modern C++ (`-std=c++26`, RTTI disabled). It decides whether a propositional formula in conjunctive normal
form is satisfiable, produces a model when it is, and can emit a machine-checkable refutation proof when it
is not. It is single-threaded, has no runtime dependencies beyond the standard library, and is used three
ways: as a DIMACS command-line solver, as a C++ library through the `kmx::sat::solver` facade, and as a C
library through a seven-function subset of the IPASIR incremental interface.

The design draws on two lineages. From **CaDiCaL** it takes architectural breadth — an incremental API,
proof generation in six formats, a preprocessing/inprocessing pipeline, and model reconstruction through an
extension-stack journal. From **Kissat** it takes mechanical sympathy — a virtual-memory-backed bump
allocated clause arena, 32-bit compressed clause references, a 16-byte clause header co-located with its
literals, direct-indexed watch lists, and an in-place propagation loop. Unlike CaDiCaL's monolithic
`Internal` struct or Kissat's macro-heavy C99, it is a composition root over concrete, non-virtual
components that interact through direct references, header-inlined accessors, and non-owning
function-pointer callbacks: there is no virtual dispatch on the hot path, and disabled RTTI makes that
structural rather than aspirational. Namespaces mirror the directory tree and each is a separate static
library, so a dependency violation fails the link rather than passing review.

## Capabilities

* **CDCL search** — two-watched-literal unit propagation, 1-UIP conflict analysis, recursive clause
  minimization, non-chronological backjumping, Luby-scheduled restarts with trail reuse, and a three-tier
  clause database whose reduction passes compact the arena in place.
* **Branching and phases** — an EVSIDS activity heap with phase saving that persists across incremental
  episodes, so a session resumes near its previous model.
* **Local search** — a deterministic probSAT/WalkSAT portfolio that seeds the opening phases and re-walks
  at doubling intervals on a budget proportional to search effort; it decides two corpus instances that
  neither reference solver finishes.
* **Memory** — a 4 GB `mmap` arena reservation (Linux; a growable vector elsewhere) that removes clause
  relocation from the steady state, plus a memory governor that bounds the database under pressure.
* **Simplification** — a scheduled preprocessing and inprocessing pipeline: transitive reduction,
  failed-literal probing, forward subsumption, gate extraction, vivification, bounded variable addition
  (factoring), SCC/equivalent-literal decomposition, congruence closure, and bounded variable elimination.
  Passes are individually selectable through a bitmask; the ones that are not yet sound or not yet
  implemented are off by default and named in [§12 Known limitations](documentation/technical/12-known-limitations.md).
* **Incremental solving** — clauses, learned clauses, activities and phases persist across episodes;
  assumptions are entered as real decision levels, failed cores are extracted from the reason graph, and
  models are reconstructed across eliminated variables through the extension journal.
* **Proofs** — DRAT, LRAT, FRAT, IDRUP, LIDRUP and VeriPB tracers behind a `std::variant` dispatch that
  costs nothing when no consumer is attached, with online and LRAT checkers and toolchain integration
  (`drat-trim`, `lrat-check`, `veripb`). Simplification passes the active format cannot express are
  suppressed rather than emitting an unverifiable proof.
* **Telemetry and control** — statistics counters, EMA trackers and profile clocks; conflict and decision
  limits; a cooperative terminate callback polled at deterministic safe points; a learned-clause callback.

## Standing against CaDiCaL and Kissat

Measured 2026-09-07 with `benchmarks/run_solver_comparison.py` at its defaults: idle machine, one core
pinned, statically linked release build, 30 s timeout, median of up to ten interleaved runs per instance,
references measured in the same run. Totals charge a timeout at the 30 s limit.

| Set | kmx-sat | CaDiCaL 3.0.1 | Kissat 4.0.4 |
| :--- | ---: | ---: | ---: |
| Pinned corpus (22 instances) | **2.0 s, 22/22** | 71.7 s, 20/22 | 82.8 s, 20/22 |
| Held-out random 3-SAT (40 generated) | **8.5 s, 40/40** | 30.6 s, 40/40 | 27.5 s, 40/40 |
| Classic SATLIB/DIMACS (68 instances) | **32.7 s, 67/68** | 100.0 s, 65/68 | 92.5 s, 66/68 |

These are 1990s and 2000s SATLIB and DIMACS families; the largest formula in them is 39,598 variables. The
modern workload lives in `benchmarks/corpus/competition` — the four main tracks of SAT Competition
2023-2026, 1,291 instances with competition-verified statuses, fetched on demand — where the signal is the
solved count, not per-instance milliseconds. See
[§11.3 Benchmarks](documentation/technical/11-build-test-benchmark.md#113-benchmarks) for the per-instance
tables and for why the pinned corpus must not be used as a tuning objective.

## Using it

```bash
qbs build -f source/source.qbs -d source/build/release qbs.buildVariant:release
```

QBS, C++26, GCC 15+ or Clang 19+; release is `-O3 -flto=auto`, debug adds AddressSanitizer.

```console
$ kmx-sat problem.cnf
s SATISFIABLE
v 1 -2 3 -4 … 0
```

Exit codes follow the SAT competition convention: `10` satisfiable, `20` unsatisfiable, `0` unknown.
Independent model verification runs before the model is printed unless `--no-verify` is passed. Options
cover assumptions, conflict and decision limits, restart and reduction schedules, the local-search budget
and the simplification pass mask — see
[§10.4](documentation/technical/10-configuration.md#104-cli).

From C++, link `kmx-sat-lib` and include `source/library/api`:

```cpp
kmx::sat::solver s;
s.add_clause(clause_literals);
const auto result = s.solve(kmx::sat::solve_request {});
if (result.status_of() == kmx::sat::solve_result::status::satisfiable)
    /* … s.value_of(v) … */;
```

[Appendix A](documentation/technical/appendix-a-worked-examples.md) has the complete examples, including
incremental episodes with assumptions and failed-core extraction.

## Maturity

The CDCL core, memory architecture, local search, C++ facade, IPASIR subset, incremental sessions, proof
tracers and checkers, and eight simplification passes are working and benchmarked. Five simplification
passes are scaffolding that does not yet transform the formula, bounded variable elimination is unsound
under incremental use, the `runtime` namespace (portfolio, parallel preprocessing, clause exchange) is
scaffolding, and several headers compile without being reachable from the shipped path. The technical
reference states this per component rather than describing intent:
[§1.2 Component maturity](documentation/technical/01-overview.md#12-component-maturity) and
[§12 Known limitations](documentation/technical/12-known-limitations.md) are the authoritative lists.

## Documentation

The documentation lives in [documentation/](documentation/README.md).

* [Technical reference](documentation/technical/README.md) — architecture, CDCL hot loop, memory
  model, simplification, proof pipeline, build and test.

## License

Dual licensed under the GNU General Public License v3.0 and a commercial license. See
[LICENSE](LICENSE).
