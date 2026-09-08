# KMX SAT Solver: Architecture & Technical Reference

**Applies to:** working tree at `f15791d` plus the 2026-09-06 engine rewrite · **Last verified:** 2026-09-06

This document is the architectural specification and technical reference for the **KMX SAT Solver**
(`kmx-sat`): its design philosophy, data structures, algorithmic pipelines, memory model, configuration
surface, and the implementation decisions behind them.

Every structural claim below was checked against the source at the revision named above. Where a
subsystem is scaffolding rather than a working algorithm, or where a feature is narrower than its name
suggests, this document says so rather than describing the intended design — see
[§1.2 Component maturity](01-overview.md#12-component-maturity) and
[§12 Known limitations](12-known-limitations.md#12-known-limitations-and-correctness-caveats).

---

## Table of Contents

- **[1. Overview](01-overview.md)**
  - [1.1 What it is](01-overview.md#11-what-it-is)
  - [1.2 Component maturity](01-overview.md#12-component-maturity)
  - [1.3 System architecture](01-overview.md#13-system-architecture)
  - [1.4 Namespaces and build units](01-overview.md#14-namespaces-and-build-units)
  - [1.5 Declared but unwired components](01-overview.md#15-declared-but-unwired-components)
- **[2. Public API and External Integration](02-public-api.md)**
  - [2.1 C++ facade: `kmx::sat::solver`](02-public-api.md#21-c-facade-kmxsatsolver)
  - [2.2 `solve_request` and `solve_result`](02-public-api.md#22-solve_request-and-solve_result)
  - [2.3 IPASIR C ABI](02-public-api.md#23-ipasir-c-abi)
- **[3. Memory Architecture and Physical Storage](03-memory-architecture.md)**
  - [3.1 Virtual memory arena: `bank::arena`](03-memory-architecture.md#31-virtual-memory-arena-bankarena)
  - [3.2 Clause representation: one sixteen-byte header](03-memory-architecture.md#32-clause-representation-one-sixteen-byte-header)
  - [3.3 Compressed references: `clause::ref_t`](03-memory-architecture.md#33-compressed-references-clauseref_t)
  - [3.4 Watch entries: `cdcl::watch`](03-memory-architecture.md#34-watch-entries-cdclwatch)
  - [3.5 Clause database and three-tier management](03-memory-architecture.md#35-clause-database-and-three-tier-management)
  - [3.6 Memory governor](03-memory-architecture.md#36-memory-governor)
- **[4. CDCL Hot Loop and Search Engine](04-cdcl-hot-loop.md)**
  - [4.1 Search lifecycle](04-cdcl-hot-loop.md#41-search-lifecycle)
  - [4.2 Two-watched-literal propagation](04-cdcl-hot-loop.md#42-two-watched-literal-propagation)
  - [4.3 Conflict analysis and learning](04-cdcl-hot-loop.md#43-conflict-analysis-and-learning)
  - [4.4 Clause minimization](04-cdcl-hot-loop.md#44-clause-minimization)
  - [4.5 Backjumping, restarts and trail reuse](04-cdcl-hot-loop.md#45-backjumping-restarts-and-trail-reuse)
- **[5. Branching Heuristics and Adaptive Scheduling](05-branching-and-scheduling.md)**
  - [5.1 Branching: the EVSIDS heap](05-branching-and-scheduling.md#51-branching-the-evsids-heap)
  - [5.2 Phase selection and local-search seeding](05-branching-and-scheduling.md#52-phase-selection-and-local-search-seeding)
  - [5.3 Restarts: `controller::restart`](05-branching-and-scheduling.md#53-restarts-controllerrestart)
  - [5.4 Database reduction: `controller::reduce`](05-branching-and-scheduling.md#54-database-reduction-controllerreduce)
- **[6. Simplification: Preprocessing and Inprocessing](06-simplification.md)**
  - [6.1 Simplification pass inventory](06-simplification.md#61-simplification-pass-inventory)
  - [6.2 Preprocessing pipeline](06-simplification.md#62-preprocessing-pipeline)
  - [6.3 Inprocessing epochs](06-simplification.md#63-inprocessing-epochs)
  - [6.4 The implemented passes](06-simplification.md#64-the-implemented-passes)
  - [6.5 Bounded variable elimination, and why it is off by default](06-simplification.md#65-bounded-variable-elimination-and-why-it-is-off-by-default)
  - [6.6 Model reconstruction and witness verification](06-simplification.md#66-model-reconstruction-and-witness-verification)
- **[7. Incremental Solving and the External Boundary](07-incremental-solving.md)**
  - [7.1 What persists across episodes](07-incremental-solving.md#71-what-persists-across-episodes)
  - [7.2 External and internal variables: `variable_mapper`](07-incremental-solving.md#72-external-and-internal-variables-variable_mapper)
  - [7.3 The assumption path](07-incremental-solving.md#73-the-assumption-path)
  - [7.4 Failed-core extraction](07-incremental-solving.md#74-failed-core-extraction)
  - [7.5 Model reconstruction across episodes](07-incremental-solving.md#75-model-reconstruction-across-episodes)
  - [7.6 Incremental caveats](07-incremental-solving.md#76-incremental-caveats)
- **[8. Proof Pipeline and Verification](08-proof-pipeline.md)**
  - [8.1 Physical `ref_t` versus logical `proof::clause::id`](08-proof-pipeline.md#81-physical-ref_t-versus-logical-proofclauseid)
  - [8.2 Zero-cost when no proof is attached](08-proof-pipeline.md#82-zero-cost-when-no-proof-is-attached)
  - [8.3 Tracer dispatch](08-proof-pipeline.md#83-tracer-dispatch)
  - [8.4 Format matrix](08-proof-pipeline.md#84-format-matrix)
- **[9. I/O, Runtime, and Telemetry](09-io-runtime-telemetry.md)**
  - [9.1 I/O: `kmx::sat::io`](09-io-runtime-telemetry.md#91-io-kmxsatio)
  - [9.2 Runtime: `kmx::sat::runtime`](09-io-runtime-telemetry.md#92-runtime-kmxsatruntime)
  - [9.3 Telemetry: `kmx::sat::telemetry`](09-io-runtime-telemetry.md#93-telemetry-kmxsattelemetry)
- **[10. Configuration Reference](10-configuration.md)**
  - [10.1 `solver::set_option`](10-configuration.md#101-solverset_option)
  - [10.2 Configuration profiles](10-configuration.md#102-configuration-profiles)
  - [10.3 Simplification pass mask](10-configuration.md#103-simplification-pass-mask)
  - [10.4 CLI](10-configuration.md#104-cli)
- **[11. Build, Test, and Benchmark](11-build-test-benchmark.md)**
  - [11.1 Build](11-build-test-benchmark.md#111-build)
  - [11.2 Tests](11-build-test-benchmark.md#112-tests)
  - [11.3 Benchmarks](11-build-test-benchmark.md#113-benchmarks)
- **[12. Known Limitations and Correctness Caveats](12-known-limitations.md)**
- **[13. Implementation Decisions Matrix](13-implementation-decisions.md)**
- **[Appendix A. Worked Examples](appendix-a-worked-examples.md)**
  - [A.1 Minimal C++ use](appendix-a-worked-examples.md#a1-minimal-c-use)
  - [A.2 Incremental episodes with assumptions](appendix-a-worked-examples.md#a2-incremental-episodes-with-assumptions)
  - [A.3 CLI](appendix-a-worked-examples.md#a3-cli)
- **[Appendix B. Glossary](appendix-b-glossary.md)**
- **[Appendix C. Where to Look](appendix-c-where-to-look.md)**
