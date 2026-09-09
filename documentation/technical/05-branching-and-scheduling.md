# 5. Branching Heuristics and Adaptive Scheduling

*Part of the [KMX SAT Solver technical reference](README.md).*

## 5.1 Branching: the EVSIDS heap

Branching is one EVSIDS heap, [var_heap.hpp](../../source/library/inc/kmx/sat/cdcl/var_heap.hpp): a binary
max-heap of variable indices over a flat activity array, with a position array for O(1) membership. A bump
adds the shared increment and sifts; the increment grows by `1/0.95` per conflict, and everything is rescaled
together when it approaches `1e150`, which preserves the ordering exactly. A periodic renormalization by
`0.5` runs every `decision_conflict_maintenance_interval` conflicts (default 16) for the same reason.

The search keeps the invariant that **every unassigned variable is in the heap**: all variables are pushed
at episode start, backtracking pushes back what it unassigns, and a popped variable that turns out to be
assigned is simply discarded. The heap being empty is therefore the satisfiability test — there is no scan
over the variables and no selectability callback.

The earlier blend of EVSIDS, VMTF and optional CHB (`engine::decision`) is no longer on the search path; the
`chb_enabled` and `decision_*_decay_interval` options are accepted and stored but have no effect on the
search.

## 5.2 Phase selection and local-search seeding

- **Phase saving.** `assign` records each variable's polarity in `saved_phase_`, and a decision reuses it.
  Saved phases persist across episodes, so an incremental session resumes near its previous model.
- **Local search** ([local_search.hpp](../../source/library/inc/kmx/sat/cdcl/local_search.hpp)) provides the
  phases that matter. Two flip rules are available, because no single one is robust across formula
  families: **probSAT** (choose a variable of a falsified clause with probability `(0.9 + break)^-2.06`),
  the state of the art for uniform random k-SAT, and **WalkSAT/SKC** (zero-break variable if any, else with
  the noise probability a random variable of the clause, else the least-break variable), which with 12–25%
  noise solves structured encodings on which probSAT stalls. Break counts are maintained incrementally
  through each clause's critical variable, so a flip costs one pass over the flipped variable's occurrence
  lists. The generator is deterministic.
- **Opening walk.** When the formula has 500–2,000,000 original clauses and at most 200,000 variables, a
  budget of 4,000 flips per variable is split over two rules: probSAT first on pure 3-SAT, WalkSAT first
  otherwise. Each rule runs in ten rounds restarted from the best assignment so far and stops after two
  rounds without a new best. Two guards decide whether it runs at all. The search goes first for
  **2,000 conflicts**: every planning, circuit and bounded-model-checking formula of the classic set that
  the walk cannot help is finished within that probe in milliseconds, while the walk winners pay a few
  hundredths of a second for it. And the budget is capped at **10 million flips**: every formula the walk
  has ever solved has at most a few thousand variables, whereas on a 10,000–40,000-variable structured
  formula the per-variable budget was tens of millions of flips that crept from seven unsatisfied clauses
  to one and never reached zero, at one to twelve seconds per instance. Above the cap the walk is left to
  the re-walk schedule. The walk starts from the phases as they were at solve entry, not from what the
  probe leaves behind (the probe's phase-saved assignment is a local minimum the walk climbs out of slowly;
  starting there turned a 0.2 s solve of `lran_f2000` into a timeout), and the walker is prepared on the
  formula **as stated, before preprocessing**: factoring rewrites at-most-one constraints into definitions
  of fresh variables, and on that formula the walk that solves `gcp125_17` in a third of a second finds
  nothing in thirty. The best assignment seeds the saved phases of the original variables; each variable
  factoring introduced is then phased from its recorded group (true exactly when every literal of the group
  is), so a walk that satisfies every original clause is confirmed by the search without a conflict
  (14,302 conflicts on `gcp125_17` before that completion, 2,001 after).
- **Re-walks.** At restarts, after 1,000 conflicts and then at doubling intervals, another walk starts from
  the current saved phases using whichever rule produced the best assignment so far. Its budget is a share
  of the search effort since the previous walk (8% of watch visits, at about fifteen visits per flip), with a
  floor of 250 flips per variable capped at one million flips (uncapped, every re-walk on a 40,000-variable
  formula was a ten-million-flip walk and cost `bmc-ibm-12` five seconds), scaled by an adaptive factor that doubles after a walk that improves on
  every walk before it and halves otherwise (bounded to `[1/8, 4]`). Phases are overwritten **only** by an
  improving walk: a walk that merely reaches a different local minimum would redirect a search that was
  making progress, which measured worse than the walk's own cost.

Measured on the corpus: `lran_f2000` (2,000-variable random 3-SAT near the threshold) falls to the opening
probSAT walk in 0.23 s and `gcp125_17` (graph colouring, 66k clauses) to the WalkSAT opening rounds in
0.37 s; CaDiCaL and Kissat time out on both. On unsatisfiable random 3-SAT the walks cost a few percent, and
on the structured classic families they now cost nothing measurable.

`controller::rephase` remains unwired; the re-walks play its role. The inprocessing epochs (forward
subsumption and vivification every 2,000 conflicts, backing off after two unproductive epochs) are
essential on `bmc-ibm-13` (1.00 s without them against 0.22 s) and a net cost on `hanoi5` and random
unsatisfiable formulas (10-25%), where the subsumer now skips the pairs of clauses it has already checked
(DEC-48).

An episode in which failed-literal probing derived a unit runs no walk at all, neither the opening walk
nor a re-walk. Every formula the walk has solved (random k-SAT, colouring) yields no unit to probing; the
ones whose walks stall (`hanoi4`: 718k flips at two unsatisfied clauses for a search that ended 230
conflicts after the walk started; the BMC and blocks-world instances, whose re-walks ended at 8-20) yield
dozens to hundreds. The lucky check restores the phase snapshot taken before probing on every formula,
whatever the walk's status; restoring only when an opening walk was pending had let the last lucky
attempt's phases seed the search of every formula above 2,500 variables.

## 5.3 Restarts: `controller::restart`

`controller::restart` keeps the schedule; `solver_core::restart` performs the restart with trail reuse
([§4.5](04-cdcl-hot-loop.md#45-backjumping-restarts-and-trail-reuse)). Two triggers exist:

- **Luby-scheduled conflict trigger.** The budget for restart *n* is `restart_interval × luby(n)` over the
  sequence 1, 1, 2, 1, 1, 2, 4, … (capped at exponent 24). The base interval defaults to **4,096**
  conflicts; a sweep over 512–8,192 on the generated held-out set was flat. The search alternates two
  modes over that schedule, as CaDiCaL and Kissat do: a **focused** mode with base 64 and the **stable**
  mode with the configured base, in periods that start at 2,000 conflicts and double, focused first.
  Satisfiable structured formulas want frequent restarts (`hanoi5`: 30k conflicts at a 50-conflict interval
  against 109k at 4,096) and random 3-SAT wants rare ones; the alternation halves the structured classic
  subset for a 9% cost on the random held-out set.
- **Glue EMA trigger** (`glue_restart_threshold_percent`, default **0 = disabled**): a restart fires when the
  fast glue average exceeds the slow one by the configured ratio. Enabling it (110–125%) was measured worse
  on random 3-SAT in every combination tried, so it stays off.

A decision-count trigger exists (`decision_restart_interval`, Luby-scaled the same way) but defaults to `0`.

## 5.4 Database reduction: `controller::reduce`

After minimization, the variables in the reasons of the learned clause's literals are bumped along with the
analyzed ones when the clause has at most 32 literals (reason-side bumping, as in CaDiCaL and Kissat); the
held-out set went from 10.3 s to 8.9 s with it. Before any search, the solver also tries the four trivial
assignments (each polarity in forward and backward variable order, every variable a decision followed by
propagation, root units first); an attempt that assigns every variable without a conflict is the model.

The solve entry has two phases. The first builds the propagation state on the formula as stated and runs
the lucky check, root propagation and a search budgeted at two propagated assignments per literal of the
formula, the lucky check's own attempts not counted (capped at 500 conflicts, below the reduction
interval; inprocessing is refused inside it); if
that decides the instance, no preprocessing runs at all, which is the case for most of the classic
circuit, planning and scheduling instances. If not, the arena's active bytes (propagation permutes the
literal order inside clauses as watches move), the branching heap, the saved phases and the episode
counters are restored from snapshots, so the second phase, the pipeline followed by the search, is exactly
the run it would have been without the first; the decision counts are identical to a build without the
phase (`regression_soundness_test` pins this on a pigeonhole formula). The walker's occurrence lists are
built only once the pipeline is about to run, and one scan of the stated formula serves the scheduler,
the walker and the first phase's budget. Then, before the search, every unassigned variable with a binary
occurrence is probed in both polarities with the search's own propagator: a failed probe makes its negation a root unit, and a literal implied by both
polarities is lifted to a unit. The budget is a small multiple of the clause count for the first round and
four times that for each round after a productive one, so a random formula without binary clauses skips
it and the IBM instances (`bmc-ibm-13`: 131 units) get the full pass. A root conflict found this way, or
by the lucky check, is reported directly: the propagator counts the literal that exposed a conflict as
propagated and does not rediscover it.

Reduction runs every **1,000 conflicts** by default and removes **75%** of the ranked candidates (the earlier
4,000 / 50% cost 19% more on the held-out unsatisfiable set). A clause that served as a reason since the
previous pass is **not a candidate**; a mid-glue clause that was used keeps one further pass of grace, a
high-glue one must earn its place again. That protection is what structured formulas needed: at a fixed
interval without it `hole9` took a million conflicts and `bmc-ibm-12` thirty thousand, relearning what
each pass had thrown away, and growing schedules (CaDiCaL's arithmetic growth, a square-root variant) that
halved those counts cost the random held-out set 9–20%, because a random formula wants its learned set
small and fresh. With protection the fixed interval gets the structured gain at no cost on the random
set (held-out 10.42 s → 10.44 s). A pass, `solver_core::reduce`:

1. Flags every trail reason so it cannot be selected or flushed.
2. Ranks redundant clauses with one precomputed 64-bit key per clause — tier, glue, fewer uses, lower
   activity, larger size — and sorts the keys; the earlier comparator re-read two clause headers per
   comparison and was most of a pass.
3. Marks the worst quota garbage, flushes them, recomputes tiers from current glue, halves activity and usage.
4. Clears the reason flags and calls `collect_garbage`: in-place arena compaction with a forwarding table,
   then rewrites every watch entry (dropping those of deleted clauses), every trail reason and every unit
   reference.

The arena therefore never accumulates dead clauses, and the propagation loop never meets a watch of a deleted
clause.

---

[← 4. CDCL Hot Loop and Search Engine](04-cdcl-hot-loop.md) · [Index](README.md) · [6. Simplification: Preprocessing and Inprocessing →](06-simplification.md)
