# IPASIR API Compatibility

The public C surface is an opaque-handle adapter defined by `source/library/api/kmx/sat/ipasir.h`.
Each handle owns one solver session and must be released with `ipasir_release` exactly once.

| Operation or case | Result | State effect |
| --- | --- | --- |
| `ipasir_init()` | Non-null handle, or allocation failure | Starts an empty session and preserves no clauses from an earlier session. |
| `ipasir_release(NULL)` | No-op | Safe for cleanup paths. |
| `ipasir_add(handle, lit)` | No return value | Appends a literal; `0` terminates the current clause. `INT32_MIN` is ignored as an invalid literal. |
| `ipasir_assume(handle, lit)` | No return value | Adds an assumption for the next solve; `0` and `INT32_MIN` are ignored. |
| `ipasir_solve(handle)` | `10` SAT, `20` UNSAT, `0` UNKNOWN/terminated | Completes one solve episode. |
| `ipasir_solve(NULL)` | `0` | Safe neutral result. |
| `ipasir_val(handle, lit)` | Signed assigned literal, or `0` when unavailable/invalid | Read-only; valid after SAT. `0` and `INT32_MIN` return `0`. |
| `ipasir_failed(handle, lit)` | `1` for a failed assumption, otherwise `0` | Read-only; meaningful after UNSAT under assumptions. `0` and `INT32_MIN` return `0`. |
| Empty formula | `10` | A zero-variable formula is satisfiable. |
| Empty clause (`ipasir_add(handle, 0)`) | `20` | The formula is unsatisfiable. |
| Duplicate or tautological clause | Normal SAT/UNSAT result | Input normalization remains solver-defined; tautologies do not force UNSAT. |
| Reinitialization | No return value | `ipasir_init` on the C ABI creates a new handle. The native adapter's `ipasir_init` resets its embedded session. |

## Cancellation and Timeouts

The C ABI has no callback registration or timeout parameter. Callers that need bounded execution must use the native
C++ `solver` API and register `set_terminate`; a callback returning `true` causes the solve to return the
`terminated` status, which the C++ adapter maps to IPASIR status `0`. The callback is polled at solver safe points and
must not mutate the solver or throw exceptions.

`SIGINT` and `SIGTERM` handling is provided by the runtime signal controller, not installed implicitly by the public
IPASIR handle. The runtime regression exercises both OS signals and verifies that they set only a pending flag until
the solver-side safe point consumes it. Applications own signal-handler installation and should request orderly
cancellation through their runtime integration before destroying a handle. No public C callback guarantee is made.

The compatibility matrix is exercised by the `public IPASIR compatibility matrix` Catch2 test and the C11
`kmx-sat-c-api-smoke` QBS product.

Generate the ABI artifact from a built library with:

```text
python3 benchmarks/generate_api_abi_report.py \
	--library /path/to/libkmx-sat-lib.a \
	--output api-abi-report.json
```

The report compiles the public header with `-Wall -Wextra -Wpedantic -Werror` and verifies all seven exported symbols.