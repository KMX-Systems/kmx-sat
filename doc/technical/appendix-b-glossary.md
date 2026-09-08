# Appendix B. Glossary

*Part of the [KMX SAT Solver technical reference](README.md).*

| Term | Meaning |
| :--- | :--- |
| **BCP** | Boolean Constraint Propagation; the unit-propagation loop |
| **2WL** | Two-watched-literals; a clause is only re-examined when one of its two watched literals is falsified |
| **Blocking literal** | A literal cached in the watch entry; if it is already true the clause is satisfied and need not be read |
| **Glue / LBD** | Literal Blocks Distance: the number of distinct decision levels in a learned clause. Lower is better; it is the primary retention signal |
| **1-UIP** | First Unique Implication Point; the single variable at the current decision level that all conflict paths pass through. The learned clause asserts its negation |
| **Backjump** | Non-chronological backtrack directly to the second-highest level in the learned clause, skipping intervening levels |
| **Asserting literal** | The 1-UIP literal, stored at index 0 so it propagates immediately after the backjump |
| **Trail** | The chronological stack of assignments, with decision-level boundaries |
| **Reason clause** | The clause that forced an implied assignment; the edge set of the implication graph |
| **EVSIDS** | Exponential Variable State Independent Decaying Sum; activity-based branching |
| **VMTF** | Variable Move To Front; queue-based branching, conflict participants moved to the head |
| **CHB** | Conflict History-Based branching; rewards variables that produce conflicts quickly |
| **Phase saving** | Reusing a variable's last assigned polarity when it is next decided |
| **Luby sequence** | 1, 1, 2, 1, 1, 2, 4, … — restart interval multipliers whose unbounded growth preserves completeness |
| **Subsumption** | C₁ ⊆ C₂ makes C₂ redundant |
| **Self-subsuming resolution** | Strengthening: removing a literal from C₂ when C₁ differs only by that literal's negation |
| **BVE** | Bounded Variable Elimination; resolving a variable away when the resolvent count does not grow |
| **BCE** | Blocked Clause Elimination; removing a clause all of whose resolvents on some literal are tautologies |
| **CCE** | Covered Clause Elimination; the asymmetric extension of BCE |
| **ELS / SCC** | Equivalent Literal Substitution via Strongly Connected Components of the binary implication graph |
| **Vivification** | Shortening a clause by trial-propagating its literals |
| **Backbone** | A literal true in every model of the formula |
| **Extension stack** | The journal of eliminating transformations, replayed in reverse to reconstruct a full model |
| **RUP** | Reverse Unit Propagation; the redundancy criterion underlying DRAT |
| **DRAT / LRAT / FRAT** | Unsatisfiability proof formats; LRAT adds explicit antecedent chains, FRAT adds hints |
| **IDRUP / LIDRUP** | Incremental proof formats with episode boundary records |
| **Arena** | The bump-allocated clause region; `ref_t` is an offset into it |
| **Tier** | Learned-clause retention class (0 core, 1 retained, 2 reduction target) derived from current glue |

---

[← Appendix A. Worked Examples](appendix-a-worked-examples.md) · [Index](README.md) · [Appendix C. Where to Look →](appendix-c-where-to-look.md)
