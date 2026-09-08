# 10. Configuration Reference

*Part of the [KMX SAT Solver technical reference](README.md).*

## 10.1 `solver::set_option`

`set_option(option_id id, std::int64_t value)`. Every option is an `option_id` enumerator, so a C++ caller
cannot name one that does not exist; the C API takes the text name and resolves it with `parse_option_id`,
ignoring anything unrecognized. A set option re-applies core configuration immediately and persists across
`reset_session()` until `clear_persisted_configuration()`.

| Option (`option_id` enumerator, and its C API name) | Default | Accepted values | Effect |
| :--- | ---: | :--- | :--- |
| `conflict_limit` | 0 | `>= 0` | Conflicts before `unknown`; 0 = unlimited |
| `decision_limit` | 0 | `>= 0` | Decisions before `unknown`; 0 = unlimited |
| `enabled_pass_mask` | scheduler default | any `uint64` | Simplification pass selection ([§10.3](#103-simplification-pass-mask)) |
| `strict_mode` | off | `0` / non-zero | Reject malformed input rather than tolerating metadata irregularities |
| `statistics_verbose_reporting` | compact | `0` = compact, else verbose | Detail level of emitted statistics lines |
| `decision_conflict_maintenance_interval` | 16 | `>= 0` | EVSIDS renormalization cadence; 0 leaves only the overflow-driven rescale |
| `decision_chb_decay_interval` | 8 | `>= 0` | Accepted and stored; CHB is not part of the search engine |
| `decision_restart_decay_interval` | 4 | `>= 0` | Accepted and stored; CHB is not part of the search engine |
| `restart_interval` | 4096 | `>= 0` | Luby base interval in conflicts; 0 disables |
| `decision_restart_interval` | 0 | `>= 0` | Luby base interval in decisions; 0 disables |
| `reduction_interval` | 1000 | `>= 0` | Conflicts between reduction passes; 0 disables |
| `inprocess_conflict_window` | 2000 | `> 0` | Conflicts between inprocessing epochs (adaptive above this floor) |
| `local_search_flips_per_variable` | 4000 | `>= 0` | Flip budget of the opening walk (adaptive rounds, see §5.2); 0 disables local search |
| `local_search_effort_percent` | 8 | `>= 0` | Search-effort share of later walks; 0 disables re-walks |
| `chb_enabled` | off | `0` / non-zero | Accepted and stored; no effect on the search |
| `reduction_fraction_percent` | 75 | `1..100` (others ignored) | Share of ranked candidates deleted per pass |
| `glue_restart_threshold_percent` | 0 | `0` or `>= 101` | Glue-EMA restart ratio; **0 disables**, and values in `1..100` are rejected because the ratio must exceed 1.0 |
| `cold_storage_enabled` | off | `0` / non-zero | Moves cold clause metadata to `store::clause_cold` |
| `activity_retention_threshold_percent` | 200 (= 2.0) | `>= 0` | Activity above which a clause survives reduction; divided by 100 |

## 10.2 Configuration profiles

`set_configuration(configuration_profile_id id)` clears any persisted options first, then applies a preset.
The C API resolves the profile name with `parse_configuration_profile_id`; a name that resolves to no profile
leaves the persisted configuration cleared.

| Profile (`configuration_profile_id` enumerator, and its C API name) | Sets |
| :--- | :--- |
| `safe` | `strict_mode = true`, `enabled_pass_mask = 0` (no simplification) |
| `balanced` | `strict_mode = false`, `enabled_pass_mask = ~0` (every pass, including `bounded` and `sweep`) |
| `bounded` | `conflict_limit = 1000`, `decision_limit = 10000` (a limits profile; the name is unrelated to the `bounded` BVE pass) |
| `aggressive` | `strict_mode = false`, `enabled_pass_mask = ~0`, both limits cleared to unlimited |

> `balanced` and `aggressive` set `enabled_pass_mask = ~0ull`, which turns on `bounded` (BVE). Under the
> caveats in [§6.5](06-simplification.md#65-bounded-variable-elimination-and-why-it-is-off-by-default), do not use either
> profile for an incremental session.

## 10.3 Simplification pass mask

`enabled_pass_mask` is a bitmask over `simplify::pass_id`, `bit = 1 << ordinal`. Ordinals and per-pass
status are in the [pass inventory](06-simplification.md#61-simplification-pass-inventory). A mask of `0` means "leave the
scheduler default alone", not "disable everything" — to actually disable all passes, use the `safe`
profile, which sets the mask explicitly.

## 10.4 CLI

```
kmx-sat <cnf-file> [options]
```

Reads DIMACS CNF and prints the SAT-competition status line, plus a `v` model line when satisfiable.

| Exit code | Meaning |
| ---: | :--- |
| `10` | SATISFIABLE |
| `20` | UNSATISFIABLE |
| `0` | UNKNOWN (or `--help`) |
| `1` | Usage or input error |

| Flag | Effect |
| :--- | :--- |
| `--assume <lit>` | Assume a literal for this episode; repeatable |
| `--decision-limit <n>` | Stop after *n* decisions (0 = unlimited) |
| `--conflict-limit <n>` | Stop after *n* conflicts (0 = unlimited) |
| `--restart-interval <n>` | Conflicts between scheduled restarts |
| `--decision-restart-interval <n>` | Decisions between scheduled restarts |
| `--reduction-interval <n>` | Conflicts between clause-database reductions |
| `--reduction-fraction-percent <n>` | Share of ranked learned clauses to drop |
| `--inprocess-conflict-window <n>` | Conflicts between inprocessing epochs |
| `--local-search-flips-per-variable <n>` | Flip budget of the opening walk (0 disables local search) |
| `--local-search-effort-percent <n>` | Share of search effort spent on later walks (0 disables re-walks) |
| `--decision-conflict-maintenance-interval <n>` | EVSIDS renormalization cadence in conflicts |
| `--enabled-pass-mask <n>` | Simplification pass bitmask |
| `--glue-restart-threshold-percent <n>` | Glue-EMA restart ratio (0, or ≥ 101) |
| `--activity-retention-threshold-percent <n>` | Activity retention threshold |
| `--chb-enabled <0\|1>` | Toggle the CHB heuristic |
| `--no-model` | Suppress the `v` line on a satisfiable result |
| `--no-verify` | Skip the independent model check before printing |
| `-h`, `--help` | Usage |

Model verification is **on by default** and is the check that catches unsound simplification. Turn it off
only for timing runs.

---

[← 9. I/O, Runtime, and Telemetry](09-io-runtime-telemetry.md) · [Index](README.md) · [11. Build, Test, and Benchmark →](11-build-test-benchmark.md)
