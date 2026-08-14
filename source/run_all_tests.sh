#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
PROJECT_FILE="${PROJECT_FILE:-source/source.qbs}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build/release-max}"
SETTINGS_DIR="${SETTINGS_DIR:-$SCRIPT_DIR/build/release-max-settings}"
QBS_CONFIG="${QBS_CONFIG:-default}"
SKIP_BUILD="${SKIP_BUILD:-0}"

usage() {
    cat <<'EOF'
Usage:
  source/run_all_tests.sh
  SKIP_BUILD=1 source/run_all_tests.sh
  BUILD_DIR=source/build/release source/run_all_tests.sh

Environment:
  BUILD_DIR     QBS build directory (default: source/build/release-max)
  SETTINGS_DIR  QBS settings directory (default: source/build/release-max-settings)
  QBS_CONFIG    QBS configuration (default: default)
  SKIP_BUILD    Set to 1 to run existing test binaries without building
EOF
}

if (($# > 0)); then
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unexpected argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
fi

if ! command -v qbs >/dev/null 2>&1; then
    echo "error: qbs not found in PATH" >&2
    exit 1
fi

cd "$PROJECT_ROOT"

if [[ "$SKIP_BUILD" != "1" ]]; then
    qbs build \
        -f "$PROJECT_FILE" \
        -d "$BUILD_DIR" \
        --settings-dir "$SETTINGS_DIR" \
        "qbs.buildVariant:release"
fi

test_root="$BUILD_DIR/$QBS_CONFIG"
mapfile -d '' test_binaries < <(
    find "$test_root" \
        -type f \
        -perm -111 \
        -name 'kmx-sat-*-test' \
        -print0 | sort -z
)

if ((${#test_binaries[@]} == 0)); then
    echo "error: no test binaries found under $test_root" >&2
    exit 1
fi

for test_binary in "${test_binaries[@]}"; do
    printf '\n=== %s ===\n' "$(basename "$test_binary")"
    "$test_binary"
done

printf '\nAll %d test products passed.\n' "${#test_binaries[@]}"
