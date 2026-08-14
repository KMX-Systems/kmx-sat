#!/bin/bash
set -e

# Default parameter: repeat count for benchmark runs
REPEAT_COUNT=${1:-3}
BENCHMARK_TIMEOUT=${BENCHMARK_TIMEOUT:-120}
SOLVER_OPTIONS=${SOLVER_OPTIONS:-}

cd "$(dirname "$0")/.."
export PATH="$PWD/tools/bin:$PATH"

# Create output directory
mkdir -p benchmark-release-results-final

# Run the benchmark
INSTANCE_COUNT=$(find benchmarks/corpus -maxdepth 1 -name '*.cnf' | wc -l)
echo "Running performance benchmark (${REPEAT_COUNT} repeats x ${INSTANCE_COUNT} instances, ${BENCHMARK_TIMEOUT}s timeout)..." >&2
python3 benchmarks/run_benchmarks.py benchmarks/corpus/*.cnf \
  --manifest benchmarks/corpus/manifest.json \
    --timeout "${BENCHMARK_TIMEOUT}" \
    --command "source/build/release-benchmark-final/default/kmx-sat.d9e8dc1a/kmx-sat {instance} ${SOLVER_OPTIONS}" \
  --compare-command "cadical=tools/bin/cadical {instance}" \
  --compare-command "kissat=tools/bin/kissat {instance}" \
  --require-comparison-agreement \
  --repeat-count "${REPEAT_COUNT}" \
  --seed 20260814 \
  --configuration release-external-comparison-final \
    --output benchmark-release-results-final/pinned-corpus.json >/dev/null 2>&1

echo "Validating results..." >&2
python3 benchmarks/validate_results.py benchmark-release-results-final/pinned-corpus.json >/dev/null 2>&1

# Extract and display table
python3 - <<'PYTHON'
import json
from pathlib import Path

p = Path('benchmark-release-results-final/pinned-corpus.json')
d = json.loads(p.read_text())

# Prepare data for each solver
solvers_data = {}
for name in ('solver', 'cadical', 'kissat'):
    times = []
    for item in d['instances']:
        runs = item['solver_runs'] if name == 'solver' else item['comparison_runs'][name]
        times += [r['elapsed_ms'] for r in runs]

    total = sum(times)
    mean = total / len(times)
    max_run = max(times)
    solvers_data[name] = (total, mean, max_run)

# Display table header
print()
print("Solver                 Total Time      Mean Per Run    Max Run")
print("─" * 62)

# Display rows
rows = [
    ("kmx-sat release", "solver"),
    ("CaDiCaL 3.0.1", "cadical"),
    ("Kissat 4.0.4", "kissat"),
]

for label, key in rows:
    total, mean, max_run = solvers_data[key]
    print(f"{label:<20} {total:>8.3f} ms    {mean:>8.3f} ms    {max_run:>8.3f} ms")

print()
PYTHON
