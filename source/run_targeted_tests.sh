#!/usr/bin/env bash
set -euo pipefail

PROJECT_FILE="${PROJECT_FILE:-source.qbs}"
BUILD_DIR="${BUILD_DIR:-$TMPDIR/qbs-source-build}"
SETTINGS_DIR="${SETTINGS_DIR:-$TMPDIR/qbs-source-settings}"
QBS_CONFIG="${QBS_CONFIG:-default}"

declare -a ALL_PRODUCTS=(
  "kmx-sat-types-test"
  "kmx-sat-telemetry-test"
  "kmx-sat-cdcl-test"
  "kmx-sat-proof-test"
  "kmx-sat-simplify-test"
  "kmx-sat-runtime-test"
  "kmx-sat-io-test"
  "kmx-sat-lib-test"
)

usage() {
  cat <<'EOF'
Usage:
  run_targeted_tests.sh [--dry-run] [--print-only] [--fail-on-empty] [--from-merge-base <ref>] [--] [changed-path ...]

Behavior:
  - If changed paths are provided, those paths drive product selection.
  - If no changed paths are provided, the script uses tracked + untracked changes:
    git diff --name-only --relative HEAD
    git ls-files --others --exclude-standard
  - With --from-merge-base <ref>, changed paths come from:
    git diff --name-only --relative <ref>...HEAD
  - The script maps changed files to split test products and runs only those.

Options:
  --dry-run     Pass -n to qbs build.
  --print-only  Only print selected products and exit.
  --fail-on-empty
                Exit non-zero if no changed files are detected or no product maps.
  --from-merge-base <ref>
                Compute changed files from <ref>...HEAD.
  -h, --help    Show this help.

Examples:
  ./run_targeted_tests.sh library/proof.qbs
  ./run_targeted_tests.sh --dry-run
  ./run_targeted_tests.sh --fail-on-empty --from-merge-base origin/main
  ./run_targeted_tests.sh --from-merge-base origin/main
  ./run_targeted_tests.sh --print-only source/library/inc/kmx/sat/cdcl/solver_core.hpp
EOF
}

if ! command -v qbs >/dev/null 2>&1; then
  echo "error: qbs not found in PATH" >&2
  exit 1
fi

dry_run=false
print_only=false
fail_on_empty=false
from_merge_base_ref=""
declare -a changed_paths=()

while (($#)); do
  case "$1" in
    --dry-run)
      dry_run=true
      ;;
    --print-only)
      print_only=true
      ;;
    --fail-on-empty)
      fail_on_empty=true
      ;;
    --from-merge-base)
      shift
      if (($# == 0)); then
        echo "error: --from-merge-base requires a git ref argument" >&2
        exit 1
      fi
      from_merge_base_ref="$1"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      while (($#)); do
        changed_paths+=("$1")
        shift
      done
      break
      ;;
    *)
      changed_paths+=("$1")
      ;;
  esac
  shift
done

if ((${#changed_paths[@]} == 0)); then
  if ! command -v git >/dev/null 2>&1; then
    echo "error: git not found and no changed paths were provided" >&2
    exit 1
  fi

  declare -a tracked_changes=()
  declare -a untracked_changes=()

  if [[ -n "$from_merge_base_ref" ]]; then
    mapfile -t tracked_changes < <(git diff --name-only --relative "${from_merge_base_ref}...HEAD")
  else
    mapfile -t tracked_changes < <(git diff --name-only --relative HEAD)
  fi
  mapfile -t untracked_changes < <(git ls-files --others --exclude-standard)

  changed_paths=("${tracked_changes[@]}" "${untracked_changes[@]}")
fi

if ((${#changed_paths[@]} == 0)); then
  echo "No changed files detected. Nothing to run."
  if $fail_on_empty; then
    exit 1
  fi
  exit 0
fi

declare -A selected=()
select_all=false

add_product() {
  local product="$1"
  selected["$product"]=1
}

for raw in "${changed_paths[@]}"; do
  path="${raw#./}"

  case "$path" in
    source.qbs|library-test/unit-test.qbs|library-test/TestApplication.qbs|library-test/*.qbs|library-test/src/kmx/sat/catch2_main.cpp)
      select_all=true
      ;;

    library/types.qbs|library/api/kmx/sat/failed_core_view.hpp|library/api/kmx/sat/literal.hpp|library/api/kmx/sat/model_view.hpp|library/api/kmx/sat/solve_request.hpp|library/api/kmx/sat/variable.hpp|library/inc/kmx/sat/cdcl/clause/ref_t.hpp|library/inc/kmx/sat/proof/clause/id.hpp|library-test/src/kmx/sat/types/*)
      add_product "kmx-sat-types-test"
      ;;

    library/telemetry.qbs|library/api/kmx/sat/telemetry/*|library/inc/kmx/sat/telemetry/*|library-test/src/kmx/sat/telemetry/*)
      add_product "kmx-sat-telemetry-test"
      ;;

    library/cdcl.qbs|library/inc/kmx/sat/cdcl/*|library/inc/kmx/sat/cdcl/**/*|library/inc/kmx/sat/solver_state_machine.hpp|library-test/src/kmx/sat/cdcl/*|library-test/src/kmx/sat/solver_state_machine_test.cpp)
      add_product "kmx-sat-cdcl-test"
      ;;

    library/proof.qbs|library/inc/kmx/sat/proof/*|library/inc/kmx/sat/proof/**/*|library/api/kmx/sat/proof_manager.hpp|library-test/src/kmx/sat/proof/*)
      add_product "kmx-sat-proof-test"
      ;;

    library/simplify.qbs|library/inc/kmx/sat/simplify/*|library/inc/kmx/sat/simplify/**/*|library-test/src/kmx/sat/simplify/*)
      add_product "kmx-sat-simplify-test"
      ;;

    library/runtime.qbs|library/inc/kmx/sat/runtime/*|library/inc/kmx/sat/runtime/**/*|library-test/src/kmx/sat/runtime/*)
      add_product "kmx-sat-runtime-test"
      ;;

    library/io.qbs|library/inc/kmx/sat/io/*|library/inc/kmx/sat/io/**/*|library-test/src/kmx/sat/io/*)
      add_product "kmx-sat-io-test"
      ;;

    library/library.qbs|library/api/kmx/logger.hpp|library/api/kmx/sat/c_api_adapter.hpp|library/api/kmx/sat/solve_result.hpp|library/api/kmx/sat/solver.hpp|library/src/kmx/sat/c_api_adapter.cpp|library/src/kmx/sat/solver.cpp|library-test/src/kmx/sat/solver_facade_test.cpp)
      add_product "kmx-sat-lib-test"
      ;;
  esac
done

declare -a products=()

if $select_all; then
  products=("${ALL_PRODUCTS[@]}")
else
  for product in "${ALL_PRODUCTS[@]}"; do
    if [[ -n "${selected[$product]:-}" ]]; then
      products+=("$product")
    fi
  done
fi

if ((${#products[@]} == 0)); then
  echo "No product mapping for changed files:"
  for p in "${changed_paths[@]}"; do
    echo "  - $p"
  done
  echo "Tip: pass explicit products with qbs, or extend run_targeted_tests.sh mapping."
  if $fail_on_empty; then
    exit 1
  fi
  exit 0
fi

printf 'Selected test products (%d):\n' "${#products[@]}"
for p in "${products[@]}"; do
  echo "  - $p"
done

if $print_only; then
  exit 0
fi

products_csv="$(IFS=,; echo "${products[*]}")"

declare -a qbs_cmd=(
  qbs
  build
  -f "$PROJECT_FILE"
  -d "$BUILD_DIR"
  --settings-dir "$SETTINGS_DIR"
  "config:$QBS_CONFIG"
  --products "$products_csv"
)

if $dry_run; then
  qbs_cmd+=("-n")
fi

echo "Running: ${qbs_cmd[*]}"
"${qbs_cmd[@]}"
