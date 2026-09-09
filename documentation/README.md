# KMX SAT Solver Documentation

## Technical reference

[technical/](technical/README.md) is the definitive architectural specification, split one file per
chapter — CDCL search hot loop, memory model, heuristics, simplification passes, proof verification
pipeline, benchmarks, and implementation decisions, with Mermaid diagrams.

| Chapter | Contents |
| :--- | :--- |
| [1. Overview](technical/01-overview.md) | What it is, component maturity, system architecture, namespaces, unwired components |
| [2. Public API](technical/02-public-api.md) | `kmx::sat::solver` facade, `solve_request`/`solve_result`, IPASIR C ABI |
| [3. Memory architecture](technical/03-memory-architecture.md) | Arena, clause header, compressed refs, watches, three-tier database, memory governor |
| [4. CDCL hot loop](technical/04-cdcl-hot-loop.md) | Search lifecycle, two-watched-literal propagation, conflict analysis, minimization, backjumping |
| [5. Branching and scheduling](technical/05-branching-and-scheduling.md) | EVSIDS heap, phase selection and local-search seeding, restarts, database reduction |
| [6. Simplification](technical/06-simplification.md) | Pass inventory, preprocessing pipeline, inprocessing epochs, BVE, model reconstruction |
| [7. Incremental solving](technical/07-incremental-solving.md) | What persists across episodes, variable mapping, assumptions, failed cores, caveats |
| [8. Proof pipeline](technical/08-proof-pipeline.md) | Physical vs logical clause identity, tracer dispatch, format matrix |
| [9. I/O, runtime, telemetry](technical/09-io-runtime-telemetry.md) | DIMACS I/O, runtime services, statistics and options |
| [10. Configuration](technical/10-configuration.md) | `set_option`, profiles, simplification pass mask, CLI |
| [11. Build, test, benchmark](technical/11-build-test-benchmark.md) | QBS build, test suite, benchmark corpora and harness |
| [12. Known limitations](technical/12-known-limitations.md) | Correctness caveats, in one table |
| [13. Implementation decisions](technical/13-implementation-decisions.md) | Decision matrix with rationale |
| [Appendix A](technical/appendix-a-worked-examples.md) · [B](technical/appendix-b-glossary.md) · [C](technical/appendix-c-where-to-look.md) | Worked examples, glossary, where to look in the source |
