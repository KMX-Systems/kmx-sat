# 7. Incremental Solving and the External Boundary

*Part of the [KMX SAT Solver technical reference](README.md).*

An **episode** is one `solver::solve(request)` call. Incremental use means running many episodes against a
growing clause database with different assumptions each time, and it requires an explicit answer to one
question: what survives an episode boundary, and what does not?

## 7.1 What persists across episodes

[incremental_context.hpp](../../source/library/inc/kmx/sat/cdcl/incremental_context.hpp) exists to make that
boundary a declaration rather than an emergent property. `begin_solve_epoch` / `end_solve_epoch` bracket
one episode; `reset_transient_state` discards what was scoped to it.

| Persists | Discarded at episode end |
| :--- | :--- |
| Learned clauses marked by `retain_learned_clause` | Assumptions and any temporary constraint |
| The external↔internal variable mapping | The decision trail above level 0 |
| The option subset designated by `persist_option_subset` | Per-episode limits and the variable-selectability filter |
| The extension stack (reconstruction journal) | Conflict/decision counters for the episode |

The constraint this places on memory management is worth stating explicitly: a clause retained here must
still resolve correctly after a later compaction or GC cycle. That is precisely why clause identity is
split between `clause::ref_t` and `proof::clause::id` ([§8.1](08-proof-pipeline.md#81-physical-ref_t-versus-logical-proofclauseid)).

## 7.2 External and internal variables: `variable_mapper`

[variable_mapper.hpp](../../source/library/inc/kmx/sat/cdcl/variable_mapper.hpp) owns the external-to-internal
(e2i) and internal-to-external (i2e) tables. `ensure_external_variable` allocates or looks up the internal
slot for a caller-facing variable.

The mapping is not cosmetic. Simplification introduces variables the caller never named — `factorizer` and
gate extraction both do — and those must be drawn from a range disjoint from anything the caller can
mention later, or a subsequent `add_clause` would silently collide with an internal-only variable.
Equivalent-literal substitution moves in the other direction, rewriting the external mapping so that two
externally distinct variables resolve to one internal representative.

## 7.3 The assumption path

Assumptions occupy the **first decision levels** of an episode, one level each, in the order given. Before
branching, the search checks the next assumption: if it is already true a placeholder level is opened, if it
is unassigned it becomes that level's decision, and if it is false the episode ends with a failed core. Real
decisions start above the assumption levels, and a restart never backtracks below them.

This is what makes learned clauses reusable: because no assumption sits at level 0, every learned clause is
implied by the formula alone and can be kept for later episodes. The earlier design assigned assumptions at
level 0 and dropped level-0 literals from learned clauses, so a clause learned under an assumption survived
into episodes without it — a satisfiable follow-up episode could answer UNSAT. Assumption variables are
still frozen for preprocessing, for the reason given in the code: eliminating one discards the clauses that
constrain it.

## 7.4 Failed-core extraction

When the next assumption is found false, `solver_core::analyze_final` explains it: starting from the failed
literal, the trail is walked backwards over seen literals; a literal with a reason marks the reason's other
literals, and a literal without one is an assumption decision and joins the core. The result is the set of
assumptions that imply the negation of the failed one, plus the failed one itself (both polarities of a
variable assumed together yield exactly those two). A formula refuted at level 0 reports an **empty** core,
since no assumption took part.

> `cdcl::failed_core_extractor` is a separate class implementing the same idea; it is instantiated only inside
> `external_frontend`, which is not on the shipped solve path.

## 7.5 Model reconstruction across episodes

`build_internal_model` reads the final assignment and, **only when the extension stack is non-empty**,
replays it through `model_reconstructor`. With no eliminating pass enabled — the default, since BVE is off
— the stack is empty and reconstruction is an identity pass that costs nothing.

## 7.6 Incremental caveats

- **Do not enable BVE in an incremental session.** Eliminating a variable discards the clauses that
  constrained it; a clause added over it in a later episode has nothing to contradict it. This is
  limitation 1 in [§12](12-known-limitations.md#12-known-limitations-and-correctness-caveats), and it is why the `balanced` and
  `aggressive` profiles — which set `enabled_pass_mask = ~0` — are unsafe here.
- **`ipasir_solve` builds a default `solve_request` every call**, so a C client cannot vary limits or the
  pass mask per episode. Use the C++ facade when episodes need different budgets.
- **Learned clauses are safe to keep across episodes** since assumptions became decision levels
  ([§7.3](#73-the-assumption-path)); the previous engine leaked assumption-derived clauses.
- **`assumption_reuse_advisor` is not wired in.** The header describes trail reuse across
  assumption-heavy workloads (bounded model checking, repeated ILP-style calls), but nothing includes it,
  so every episode re-propagates its assumption prefix from scratch.

---

[← 6. Simplification: Preprocessing and Inprocessing](06-simplification.md) · [Index](README.md) · [8. Proof Pipeline and Verification →](08-proof-pipeline.md)
