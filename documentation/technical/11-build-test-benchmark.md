# 11. Build, Test, and Benchmark

*Part of the [KMX SAT Solver technical reference](README.md).*

## 11.1 Build

The project builds with **QBS** from [source/source.qbs](../../source/source.qbs), C++26, GCC 15+ or Clang 19+.

```bash
cd /path/to/kmx-sat
export PATH="$PWD/tools/bin:$PATH"      # cadical, kissat, drat-trim, lrat-check, veripb

# Optimized release build of everything, including the CLI
qbs build -f source/source.qbs -d source/build/release qbs.buildVariant:release

# Just the CLI
qbs build -f source/source.qbs -d source/build/release qbs.buildVariant:release -p kmx-sat
```

Pass `profile:gcc` (or the profile the release directory was configured with) for anything whose speed
will be compared: a build directory remembers its profile, and on a machine whose default qbs profile is
clang, a new directory silently becomes a clang build, 5-8% slower on the walk-heavy instances with the
same instruction count and inlined differently throughout. `strings <binary> | grep -c 'clang version'`
is zero for a GCC build.

Release flags are `-O3 -flto=auto` with `NDEBUG`. Two omissions are deliberate and documented in
[library.qbs](../../source/library/library.qbs):

- **`-Ofast` is not used.** `-ffast-math` relaxes the floating-point semantics the EMA trackers and clause
  activity scores depend on, and it measured as no gain — identical conflict counts, timings within noise.
- **`-march=native` is not used**, so a release build stays portable and reproducible, which is what the
  clean-checkout gates claim. Pass it explicitly when tuning for one machine.

Debug builds compile with AddressSanitizer (`-fsanitize=address`).

For a fully reproducible clean-checkout release build plus acceptance gates:

```bash
python3 benchmarks/run_clean_checkout_gates.py --seed 20260814 --repeat-count 2 --differential-cases 128
```

## 11.2 Tests

Nine QBS test products under [source/library-test](../../source/library-test), aggregated by
[unit-test.qbs](../../source/library-test/unit-test.qbs):

| Product | Covers |
| :--- | :--- |
| `kmx-sat-types-test` | `literal`, `variable`, counters |
| `kmx-sat-telemetry-test` | Statistics counters, EMA trackers, report formatting |
| `kmx-sat-cdcl-test` | BCP, 1-UIP analysis, minimizer, arena, heuristics, controllers |
| `kmx-sat-proof-test` | Event streams, checkers, tracer backends |
| `kmx-sat-simplify-test` | BVE, subsumption, gate extraction, inprocessing schedules |
| `kmx-sat-runtime-test` | Signal handling, portfolio and parallel adapters |
| `kmx-sat-io-test` | DIMACS streaming parser, binary fixture roundtrips |
| `kmx-sat-lib-test` | Solver facade, incremental replays, IPASIR compatibility |
| `kmx-sat-c-api-smoke` | C ABI link-and-run smoke test |

[run_targeted_tests.sh](../../source/run_targeted_tests.sh) maps changed files to affected products and builds
only those:

```bash
cd source
TMPDIR=/tmp ./run_targeted_tests.sh                                  # from working-tree changes
./run_targeted_tests.sh --from-merge-base origin/main                # from a branch diff
./run_targeted_tests.sh --print-only source/library/inc/kmx/sat/cdcl/solver_core.hpp
```

Three test files pin the defects found while tuning against the classic SATLIB set (2026-09-06/07):
`cdcl/regression_soundness_test.cpp` (root units and the lucky check, root conflicts found before the
search, probing units against assumptions, factoring's model projection and fresh-variable numbering, late
proof-tracer attachment, and 400 seeded random formulas decided against exhaustive enumeration with every
model verified), `cdcl/controller/reduce_protection_test.cpp` (recent-use protection and its grace) and
`simplify/pass_equivalence_test.cpp` (model-set equivalence of transitive reduction and factoring on random
formulas, promotion of a subsuming learned clause). Run the exhaustive check after any change to the
pre-search pipeline: `kmx-sat-cdcl-test "[regression]"`.

## 11.3 Benchmarks

[benchmarks/run_benchmarks.py](../../benchmarks/run_benchmarks.py) runs a pinned corpus with CPU affinity
(`taskset`), warmup runs, repeat determinism checks, manifest SHA-256 enforcement, and model validation.

```bash
export PATH="$PWD/tools/bin:$PATH"
python3 benchmarks/run_benchmarks.py benchmarks/corpus/*.cnf \
  --manifest benchmarks/corpus/manifest.json \
  --command "$PWD/source/build/release/…/kmx-sat {instance}" \
  --compare-command "cadical=$PWD/tools/bin/cadical {instance}" \
  --compare-command "kissat=$PWD/tools/bin/kissat {instance}" \
  --require-comparison-agreement \
  --repeat-count 3 \
  --seed 20260814 \
  --configuration external-solvers-release \
  --output clean-checkout-gates/external-solvers-release.json
```

Per instance and solver the report records status, exit code, elapsed time, peak RSS (when
`/usr/bin/time` is available), parsed solver statistics, repeat determinism, and comparison agreement.

**Corpus composition.** `benchmarks/corpus` holds 12 small hand-written and SATLIB `aim` instances;
`benchmarks/corpus/extended` holds 10 larger SATLIB instances (`uf250`, `uuf250`, `gcp125`, `gcp250`,
`lran`); `benchmarks/corpus/classic` holds 68 instances from the classic SATLIB and DIMACS families —
uniform and controlled-backbone random 3-SAT, flat graph colouring, Dubois, Pretolani and pigeonhole
(unsatisfiable), parity learning, inductive inference, Hooker's `jnh`, the `ssa`/`bf` circuit-fault sets,
`aim-200`, SATPLAN logistics and blocks world, IBM bounded model checking, the 1996 Beijing competition
set, large graph colouring and all-interval series. Its `manifest.json` pins the expected status of every
instance at least one reference solver decided within 60 s; `open-manifest.json` lists the rest. Run it
with `run_solver_comparison.py`, which runs it by default together with the base and extended sets
(`--set classic` restricts a run to it).

**SAT Competition main tracks (added 2026-09-08).** `benchmarks/corpus/competition` holds the four main
tracks of SAT Competition 2023-2026 — 1,291 instances with a competition-verified status, 1.6 GB of
xz-compressed CNF, fetched by
[fetch_competition_corpus.py](../../benchmarks/fetch_competition_corpus.py) from the
[Global Benchmark Database](https://benchmark-database.de) that the competition publishes through. Only the
per-track `manifest.json` is committed; the CNF payload is gitignored and fetched on demand, and the
fetcher is resumable, idempotent and `--verify`-checkable. The selection is every instance whose result is
`sat` or `unsat` and whose compressed file is at most 20 MB; without that bound the four tracks are 14.3 GB.
The tracks are opt-in sets in `run_solver_comparison.py` (`--set competition`, or `--set main_2026`) and are
absent until fetched. Instances keep the distributed `.cnf.xz` encoding: CaDiCaL and Kissat read it
directly and kmx-sat has no decompressing reader, so the harness decompresses each instance once outside
every timed round and hands all three solvers the identical plain CNF (`--scratch-dir` places the
decompressed copy).

> **This set answers a question the pinned corpora cannot.** Everything above is SATLIB and DIMACS material
> from the 1990s and 2000s; the largest formula in it is 39,598 variables and 194,778 clauses, and the whole
> 90-instance default run takes seconds. The competition instances are the modern workload the field is
> measured on: only 79 of them are solvable by MiniSat within one million conflicts, and the competition
> allows 5,000 s per instance. At a laptop-scale timeout most of them time out for all three solvers, so the
> signal is the solved count and which instances each solver decides, not per-instance milliseconds. A run is
> also a 1,291-instance soundness test against independently verified statuses.

> **Do not tune on this corpus.** Most of the pinned instances finish in 1–3 ms and measure process
> startup rather than search; only `satlib_uf250_01/050` and `satlib_uuf250_01/050` are real search
> workloads, and random 3-SAT runtimes are heavy-tailed enough that four samples cannot separate a real
> gain from luck. A restart/reduction sweep once showed a sharp "optimum" on those four that was
> indistinguishable from its neighbours on 42 generated held-out instances. **A/B every heuristic or
> schedule change on a generated random 3-SAT set** (n = 180…260, ratio 4.26 satisfiable / 4.6
> unsatisfiable, several seeds each; ~40 instances runs in about a minute). Use the pinned corpus as a
> correctness gate and a reporting number, not as the tuning objective.

**Standing against CaDiCaL and Kissat (measured 2026-09-07, after the fifth cycle,
with `run_solver_comparison.py` at its defaults: idle machine, one core pinned, statically linked release build, 30 s timeout, median of up to ten
interleaved runs per instance under a 60 s per-instance budget, references measured in the same run).**
Totals count a timeout at the 30 s limit.

### Corpus (22 instances, seconds)

| Instance | Status | kmx-sat | CaDiCaL 3.0.1 | Kissat 4.0.4 |
| :--- | :--- | ---: | ---: | ---: |
| `branching_sat.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `deep_implication.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `learned_unsat.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `proof_unsat.cnf` | ? | 0.001 | 0.002 | 0.002 |
| `restart_reduction_unsat.cnf` | ? | 0.001 | 0.001 | 0.001 |
| `sat_unit.cnf` | ? | 0.001 | 0.001 | 0.001 |
| `satlib_aim100_16_yes_01.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `satlib_aim100_20_yes_01.cnf` | ? | 0.001 | 0.002 | 0.002 |
| `satlib_aim100_34_yes_01.cnf` | ? | 0.001 | 0.002 | 0.002 |
| `satlib_aim100_no_01.cnf` | ? | 0.001 | 0.002 | 0.003 |
| `satlib_aim100_yes_01.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `satlib_gcp125_17.cnf` | ? | 0.363 | t/o | t/o |
| `satlib_gcp250_15.cnf` | ? | 0.196 | 1.010 | 13.571 |
| `satlib_lran_f2000.cnf` | ? | 0.220 | t/o | t/o |
| `satlib_lran_f600.cnf` | ? | 0.021 | 4.238 | 5.070 |
| `satlib_uf250_01.cnf` | ? | 0.010 | 0.133 | 0.032 |
| `satlib_uf250_050.cnf` | ? | 0.019 | 0.320 | 0.203 |
| `satlib_uuf250_01.cnf` | ? | 0.580 | 3.013 | 1.727 |
| `satlib_uuf250_050.cnf` | ? | 0.563 | 2.926 | 2.171 |
| `unsat_branching.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `unsat_units.cnf` | ? | 0.001 | 0.001 | 0.001 |
| `wide_clause_sat.cnf` | ? | 0.001 | 0.001 | 0.001 |
| **Total** | | **2.0 s, 22/22** | **71.7 s, 20/22** | **82.8 s, 20/22** |

### Held-out random 3-SAT (40 generated instances, 30 s timeout, median of 3 interleaved runs, seconds)

| Instance | Status | kmx-sat | CaDiCaL 3.0.1 | Kissat 4.0.4 |
| :--- | :--- | ---: | ---: | ---: |
| `r3_s_200_0.cnf` | UNSAT | 0.122 | 0.361 | 0.423 |
| `r3_s_200_1.cnf` | UNSAT | 0.111 | 0.354 | 0.259 |
| `r3_s_200_2.cnf` | SAT | 0.008 | 0.107 | 0.042 |
| `r3_s_200_3.cnf` | SAT | 0.015 | 0.062 | 0.041 |
| `r3_s_200_4.cnf` | SAT | 0.011 | 0.046 | 0.034 |
| `r3_s_220_0.cnf` | SAT | 0.013 | 0.259 | 0.035 |
| `r3_s_220_1.cnf` | UNSAT | 0.143 | 0.501 | 0.427 |
| `r3_s_220_2.cnf` | SAT | 0.008 | 0.054 | 0.034 |
| `r3_s_220_3.cnf` | UNSAT | 0.160 | 0.603 | 0.441 |
| `r3_s_220_4.cnf` | UNSAT | 0.284 | 0.953 | 0.984 |
| `r3_s_240_0.cnf` | SAT | 0.012 | 0.052 | 0.351 |
| `r3_s_240_1.cnf` | SAT | 0.013 | 0.143 | 0.035 |
| `r3_s_240_2.cnf` | SAT | 0.013 | 0.243 | 0.035 |
| `r3_s_240_3.cnf` | SAT | 0.082 | 0.402 | 1.233 |
| `r3_s_240_4.cnf` | SAT | 0.012 | 0.160 | 0.373 |
| `r3_s_260_0.cnf` | SAT | 0.013 | 0.310 | 0.669 |
| `r3_s_260_1.cnf` | SAT | 0.016 | 1.287 | 0.036 |
| `r3_s_260_2.cnf` | UNSAT | 2.498 | 9.313 | 7.660 |
| `r3_s_260_3.cnf` | UNSAT | 0.639 | 2.899 | 2.595 |
| `r3_s_260_4.cnf` | UNSAT | 2.351 | 6.084 | 6.177 |
| `r3_u_180_0.cnf` | UNSAT | 0.048 | 0.077 | 0.083 |
| `r3_u_180_1.cnf` | UNSAT | 0.022 | 0.052 | 0.066 |
| `r3_u_180_2.cnf` | UNSAT | 0.035 | 0.063 | 0.071 |
| `r3_u_180_3.cnf` | UNSAT | 0.043 | 0.070 | 0.101 |
| `r3_u_180_4.cnf` | UNSAT | 0.033 | 0.075 | 0.097 |
| `r3_u_200_0.cnf` | UNSAT | 0.066 | 0.208 | 0.166 |
| `r3_u_200_1.cnf` | UNSAT | 0.086 | 0.190 | 0.148 |
| `r3_u_200_2.cnf` | UNSAT | 0.063 | 0.212 | 0.168 |
| `r3_u_200_3.cnf` | UNSAT | 0.080 | 0.225 | 0.183 |
| `r3_u_200_4.cnf` | UNSAT | 0.065 | 0.113 | 0.118 |
| `r3_u_220_0.cnf` | UNSAT | 0.112 | 0.448 | 0.445 |
| `r3_u_220_1.cnf` | UNSAT | 0.076 | 0.160 | 0.257 |
| `r3_u_220_2.cnf` | UNSAT | 0.065 | 0.117 | 0.150 |
| `r3_u_220_3.cnf` | UNSAT | 0.053 | 0.116 | 0.117 |
| `r3_u_220_4.cnf` | UNSAT | 0.095 | 0.184 | 0.170 |
| `r3_u_240_0.cnf` | UNSAT | 0.297 | 1.536 | 0.923 |
| `r3_u_240_1.cnf` | UNSAT | 0.158 | 0.398 | 0.380 |
| `r3_u_240_2.cnf` | UNSAT | 0.176 | 0.547 | 0.732 |
| `r3_u_240_3.cnf` | UNSAT | 0.122 | 0.451 | 0.268 |
| `r3_u_240_4.cnf` | UNSAT | 0.265 | 1.137 | 0.949 |
| **Total** | | **8.5 s, 40/40** | **30.6 s, 40/40** | **27.5 s, 40/40** |

### Classic SATLIB/DIMACS set (`benchmarks/corpus/classic`, 68 instances, seconds)

| Instance | Status | kmx-sat | CaDiCaL 3.0.1 | Kissat 4.0.4 |
| :--- | :--- | ---: | ---: | ---: |
| `satlib_2bitadd_12.cnf` | ? | 0.002 | 0.005 | 0.020 |
| `satlib_2bitcomp_5.cnf` | ? | 0.001 | 0.001 | 0.001 |
| `satlib_2bitmax_6.cnf` | ? | 0.001 | 0.002 | 0.002 |
| `satlib_3bitadd_32.cnf` | ? | 0.010 | 0.246 | 0.141 |
| `satlib_4blocksb.cnf` | ? | 0.006 | 0.009 | 0.043 |
| `satlib_aim_200_2_0_no_1.cnf` | ? | 0.001 | 0.002 | 0.005 |
| `satlib_aim_200_3_4_yes1_1.cnf` | ? | 0.002 | 0.002 | 0.004 |
| `satlib_ais10.cnf` | ? | 0.002 | 0.002 | 0.002 |
| `satlib_ais12.cnf` | ? | 0.002 | 0.002 | 0.002 |
| `satlib_ais8.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `satlib_bf0432_007.cnf` | ? | 0.009 | 0.006 | 0.006 |
| `satlib_bf1355_075.cnf` | ? | 0.002 | 0.004 | 0.005 |
| `satlib_bf2670_001.cnf` | ? | 0.002 | 0.003 | 0.005 |
| `satlib_bmc_ibm_1.cnf` | ? | 0.016 | 0.044 | 0.076 |
| `satlib_bmc_ibm_12.cnf` | ? | 0.849 | 0.960 | 0.716 |
| `satlib_bmc_ibm_13.cnf` | ? | 0.214 | 0.397 | 0.183 |
| `satlib_bmc_ibm_2.cnf` | ? | 0.003 | 0.005 | 0.013 |
| `satlib_bmc_ibm_5.cnf` | ? | 0.009 | 0.013 | 0.032 |
| `satlib_bmc_ibm_7.cnf` | ? | 0.006 | 0.011 | 0.006 |
| `satlib_bw_large_b.cnf` | ? | 0.004 | 0.006 | 0.025 |
| `satlib_bw_large_c.cnf` | ? | 0.007 | 0.025 | 0.057 |
| `satlib_bw_large_d.cnf` | ? | 0.097 | 0.225 | 0.164 |
| `satlib_cbs_k3_n100_m403_b10_0.cnf` | ? | 0.002 | 0.002 | 0.011 |
| `satlib_cbs_k3_n100_m449_b90_0.cnf` | ? | 0.002 | 0.005 | 0.020 |
| `satlib_dubois100.cnf` | ? | 0.003 | 0.002 | 0.002 |
| `satlib_dubois50.cnf` | ? | 0.002 | 0.002 | 0.001 |
| `satlib_e0ddr2_10_by_5_4.cnf` | ? | 0.015 | 0.027 | 0.014 |
| `satlib_enddr2_10_by_5_8.cnf` | ? | 0.016 | 0.028 | 0.014 |
| `satlib_f1000.cnf` | ? | 0.029 | 3.820 | 28.050 |
| `satlib_flat100_1.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `satlib_flat200_1.cnf` | ? | 0.004 | 0.007 | 0.011 |
| `satlib_flat200_2.cnf` | ? | 0.006 | 0.019 | 0.040 |
| `satlib_g125_18.cnf` | ? | 0.121 | t/o | t/o |
| `satlib_hanoi4.cnf` | ? | 0.018 | 0.017 | 0.030 |
| `satlib_hanoi5.cnf` | ? | 0.345 | 0.279 | 0.402 |
| `satlib_hole10.cnf` | ? | 0.351 | t/o | 1.132 |
| `satlib_hole8.cnf` | ? | 0.012 | 0.292 | 0.043 |
| `satlib_hole9.cnf` | ? | 0.104 | 2.614 | 0.112 |
| `satlib_huge.cnf` | ? | 0.002 | 0.004 | 0.022 |
| `satlib_ii16a1.cnf` | ? | 0.004 | 0.005 | 0.003 |
| `satlib_ii32b1.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `satlib_ii32e1.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `satlib_ii8b1.cnf` | ? | 0.001 | 0.002 | 0.001 |
| `satlib_jnh1.cnf` | ? | 0.002 | 0.002 | 0.022 |
| `satlib_jnh12.cnf` | ? | 0.002 | 0.002 | 0.003 |
| `satlib_jnh201.cnf` | ? | 0.002 | 0.002 | 0.021 |
| `satlib_jnh301.cnf` | ? | 0.002 | 0.003 | 0.004 |
| `satlib_logistics_a.cnf` | ? | 0.003 | 0.004 | 0.024 |
| `satlib_logistics_c.cnf` | ? | 0.006 | 0.009 | 0.027 |
| `satlib_logistics_d.cnf` | ? | 0.007 | 0.010 | 0.029 |
| `satlib_par16_1.cnf` | ? | 0.017 | 0.055 | 0.038 |
| `satlib_par16_1_c.cnf` | ? | 0.033 | 0.032 | 0.018 |
| `satlib_par32_1_c.cnf` | ? | t/o | t/o | t/o |
| `satlib_par8_1.cnf` | ? | 0.001 | 0.002 | 0.002 |
| `satlib_pret150_25.cnf` | ? | 0.002 | 0.006 | 0.005 |
| `satlib_pret150_75.cnf` | ? | 0.003 | 0.006 | 0.005 |
| `satlib_ssa0432_003.cnf` | ? | 0.002 | 0.002 | 0.003 |
| `satlib_ssa2670_130.cnf` | ? | 0.003 | 0.004 | 0.009 |
| `satlib_ssa6288_047.cnf` | ? | 0.006 | 0.008 | 0.004 |
| `satlib_ssa7552_038.cnf` | ? | 0.002 | 0.003 | 0.002 |
| `satlib_uf150_01.cnf` | ? | 0.004 | 0.005 | 0.019 |
| `satlib_uf150_02.cnf` | ? | 0.003 | 0.006 | 0.023 |
| `satlib_uf200_01.cnf` | ? | 0.016 | 0.105 | 0.078 |
| `satlib_uf200_02.cnf` | ? | 0.012 | 0.043 | 0.131 |
| `satlib_uuf150_01.cnf` | ? | 0.024 | 0.033 | 0.041 |
| `satlib_uuf150_02.cnf` | ? | 0.028 | 0.047 | 0.065 |
| `satlib_uuf200_01.cnf` | ? | 0.100 | 0.206 | 0.251 |
| `satlib_uuf200_02.cnf` | ? | 0.096 | 0.277 | 0.226 |
| **Total** | | **32.7 s, 67/68** | **100.0 s, 65/68** | **92.5 s, 66/68** |

The held-out set is generated uniform random 3-SAT (n = 200..260 at ratio 4.26 and n = 180..240 at ratio
4.6, five seeds each); it is the tuning objective for search-policy changes, since the corpus has only four
instances that do real CDCL search. On the classic set kmx-sat solves 67 of the 68 instances, one more than
Kissat and two more than CaDiCaL, in about a third of their charged totals, and its time on the instances it solves
(3.7 s) is well under CaDiCaL's (10.0 s) and Kissat's (32.3 s). Kissat
is still faster by more than a fifth on `bmc-ibm-13` (0.50 s against 0.18 s), `hanoi5` (0.55 s against
0.39 s) and a dozen instances under 70 ms, where a few tens of milliseconds of walking or preprocessing
show against a reference that solves them in single-digit milliseconds. `par32` defeats all three solvers.

Re-run the command above to refresh these figures; they are a snapshot, not a contract.

---

[← 10. Configuration Reference](10-configuration.md) · [Index](README.md) · [12. Known Limitations and Correctness Caveats →](12-known-limitations.md)
