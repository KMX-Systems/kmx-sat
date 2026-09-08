# Appendix C. Where to Look

*Part of the [KMX SAT Solver technical reference](README.md).*

Headers declare; the translation units under [source/library/src](../../source/library/src) define. When a row
lists both, read the `.cpp` for the algorithm and the `.hpp` for the interface and the inline accessors.

| Concept | Declared in | Defined in |
| :--- | :--- | :--- |
| Public facade | [api/kmx/sat/solver.hpp](../../source/library/api/kmx/sat/solver.hpp) | [solver.cpp](../../source/library/src/kmx/sat/solver.cpp) |
| Episode input / output | [solve_request.hpp](../../source/library/api/kmx/sat/solve_request.hpp), [solve_result.hpp](../../source/library/api/kmx/sat/solve_result.hpp) | — (aggregates) |
| C ABI | [ipasir.h](../../source/library/api/kmx/sat/ipasir.h) | [c_api_adapter.cpp](../../source/library/src/kmx/sat/c_api_adapter.cpp) |
| Option and profile identity | [option_id.hpp](../../source/library/api/kmx/sat/option_id.hpp) | — (closed enums + name tables) |
| Option and profile handling | [telemetry/solver_options.hpp](../../source/library/api/kmx/sat/telemetry/solver_options.hpp) | [solver.cpp](../../source/library/src/kmx/sat/solver.cpp), [telemetry/solver_options.cpp](../../source/library/src/kmx/sat/telemetry/solver_options.cpp) |
| Episode driver, BCP loop, analysis, minimization, learning, restarts, reduction | [solver_core.hpp](../../source/library/inc/kmx/sat/cdcl/solver_core.hpp) | [solver_core.cpp](../../source/library/src/kmx/sat/cdcl/solver_core.cpp) |
| Heuristic and controller orchestration | [search_coordinator.hpp](../../source/library/inc/kmx/sat/cdcl/search_coordinator.hpp) | [search_coordinator.cpp](../../source/library/src/kmx/sat/cdcl/search_coordinator.cpp) |
| Branching heap | [var_heap.hpp](../../source/library/inc/kmx/sat/cdcl/var_heap.hpp), [evsids_heap.hpp](../../source/library/inc/kmx/sat/cdcl/evsids_heap.hpp) | [var_heap.cpp](../../source/library/src/kmx/sat/cdcl/var_heap.cpp), [evsids_heap.cpp](../../source/library/src/kmx/sat/cdcl/evsids_heap.cpp) |
| Watch entry and watch bank | [watch.hpp](../../source/library/inc/kmx/sat/cdcl/watch.hpp), [bank/watch_list.hpp](../../source/library/inc/kmx/sat/cdcl/bank/watch_list.hpp) | [bank/watch_list.cpp](../../source/library/src/kmx/sat/cdcl/bank/watch_list.cpp) |
| Restart and reduction policy | [controller/restart.hpp](../../source/library/inc/kmx/sat/cdcl/controller/restart.hpp), [controller/reduce.hpp](../../source/library/inc/kmx/sat/cdcl/controller/reduce.hpp) | [controller/restart.cpp](../../source/library/src/kmx/sat/cdcl/controller/restart.cpp), [controller/reduce.cpp](../../source/library/src/kmx/sat/cdcl/controller/reduce.cpp) |
| Local search | [local_search.hpp](../../source/library/inc/kmx/sat/cdcl/local_search.hpp) | [local_search.cpp](../../source/library/src/kmx/sat/cdcl/local_search.cpp) |
| Arena | [bank/arena.hpp](../../source/library/inc/kmx/sat/cdcl/bank/arena.hpp) | [bank/arena.cpp](../../source/library/src/kmx/sat/cdcl/bank/arena.cpp) |
| Clause storage and tiers | [clause/database.hpp](../../source/library/inc/kmx/sat/cdcl/clause/database.hpp), [clause/storage.hpp](../../source/library/inc/kmx/sat/cdcl/clause/storage.hpp), [clause/ref_t.hpp](../../source/library/inc/kmx/sat/cdcl/clause/ref_t.hpp) | [clause/database.cpp](../../source/library/src/kmx/sat/cdcl/clause/database.cpp), [clause/storage.cpp](../../source/library/src/kmx/sat/cdcl/clause/storage.cpp) |
| Incrementality | [incremental_context.hpp](../../source/library/inc/kmx/sat/cdcl/incremental_context.hpp), [variable_mapper.hpp](../../source/library/inc/kmx/sat/cdcl/variable_mapper.hpp) | [incremental_context.cpp](../../source/library/src/kmx/sat/cdcl/incremental_context.cpp), [variable_mapper.cpp](../../source/library/src/kmx/sat/cdcl/variable_mapper.cpp) |
| Model reconstruction | [stack/extension.hpp](../../source/library/inc/kmx/sat/cdcl/stack/extension.hpp), [model_reconstructor.hpp](../../source/library/inc/kmx/sat/cdcl/model_reconstructor.hpp) | [model_reconstructor.cpp](../../source/library/src/kmx/sat/cdcl/model_reconstructor.cpp) |
| Pass scheduling and the pass set | [scheduler/preprocess.hpp](../../source/library/inc/kmx/sat/simplify/scheduler/preprocess.hpp), [scheduler/inprocess.hpp](../../source/library/inc/kmx/sat/simplify/scheduler/inprocess.hpp) | [scheduler/preprocess.cpp](../../source/library/src/kmx/sat/simplify/scheduler/preprocess.cpp), [scheduler/inprocess.cpp](../../source/library/src/kmx/sat/simplify/scheduler/inprocess.cpp) |
| Pass identifiers and bit positions | [simplify/pass_id.hpp](../../source/library/inc/kmx/sat/simplify/pass_id.hpp) | — (closed enum) |
| Pass gating | [preprocessing_profile_selector.hpp](../../source/library/inc/kmx/sat/simplify/preprocessing_profile_selector.hpp) | [preprocessing_profile_selector.cpp](../../source/library/src/kmx/sat/simplify/preprocessing_profile_selector.cpp) |
| Proof events and identity | [proof_manager.hpp](../../source/library/api/kmx/sat/proof_manager.hpp), [proof/clause/id_allocator.hpp](../../source/library/inc/kmx/sat/proof/clause/id_allocator.hpp) | [proof_manager.cpp](../../source/library/src/kmx/sat/proof_manager.cpp), [proof/clause/id_allocator.cpp](../../source/library/src/kmx/sat/proof/clause/id_allocator.cpp) |
| Proof formats and checkers | [proof/format.hpp](../../source/library/inc/kmx/sat/proof/format.hpp) | — (closed enums) |
| Tracer dispatch | [proof/tracer/view.hpp](../../source/library/inc/kmx/sat/proof/tracer/view.hpp), [proof/tracer/variant_t.hpp](../../source/library/inc/kmx/sat/proof/tracer/variant_t.hpp) | [proof/tracer/view.cpp](../../source/library/src/kmx/sat/proof/tracer/view.cpp) |
| Statistics | [telemetry/solver_statistics.hpp](../../source/library/api/kmx/sat/telemetry/solver_statistics.hpp), [telemetry/report_formatter.hpp](../../source/library/inc/kmx/sat/telemetry/report_formatter.hpp) | [telemetry/solver_statistics.cpp](../../source/library/src/kmx/sat/telemetry/solver_statistics.cpp), [telemetry/report_formatter.cpp](../../source/library/src/kmx/sat/telemetry/report_formatter.cpp) |
| Timed phases | [telemetry/phase_id.hpp](../../source/library/inc/kmx/sat/telemetry/phase_id.hpp), [telemetry/profile_clock.hpp](../../source/library/inc/kmx/sat/telemetry/profile_clock.hpp) | [telemetry/profile_clock.cpp](../../source/library/src/kmx/sat/telemetry/profile_clock.cpp) |
| CLI, DIMACS parsing, model verification | — | [cli/kmx-sat-main.cpp](../../source/cli/kmx-sat-main.cpp) |

---

[← Appendix B. Glossary](appendix-b-glossary.md) · [Index](README.md)
