# Targeted Test Execution

Use `run_targeted_tests.sh` to run only the split test products affected by your changes.

## Local usage

From `source/`:

```bash
./run_targeted_tests.sh --print-only
./run_targeted_tests.sh --dry-run
./run_targeted_tests.sh library/proof.qbs
```

When no paths are passed, the script auto-detects changed files from:

- tracked changes: `git diff --name-only --relative HEAD`
- untracked files: `git ls-files --others --exclude-standard`

## CI usage

Use merge-base selection for branch diffs:

```bash
./run_targeted_tests.sh --from-merge-base origin/main --dry-run
```

Use strict mode to fail if nothing is selected:

```bash
./run_targeted_tests.sh --from-merge-base origin/main --fail-on-empty
```

## Product mapping

The script maps changed paths to these products:

- `kmx-sat-types-test`
- `kmx-sat-telemetry-test`
- `kmx-sat-cdcl-test`
- `kmx-sat-proof-test`
- `kmx-sat-simplify-test`
- `kmx-sat-runtime-test`
- `kmx-sat-io-test`
- `kmx-sat-lib-test`

Shared infra changes (for example `source.qbs`, `library-test/unit-test.qbs`, `library-test/*.qbs`, `library-test/src/kmx/sat/catch2_main.cpp`) select all products.
