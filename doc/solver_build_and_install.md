# SAT Solver Build and Local Installation

This document describes the local, non-system installation used by this workspace.
All binaries are installed under `tools/bin`; no files are written to `/usr/local`.

## Prerequisites

The builds require:

- GNU C++ compiler (`g++`)
- GNU C compiler (`gcc`)
- GNU Make (`make`)
- Python 3 and `venv`
- Network access for VeriPB Python dependencies and checker source repositories
- GMP runtime and development libraries for VeriPB

The machine used for this setup already provides `g++`, `gcc`, `make`, Python 3.12,
and GMP. Install the local tool path from the repository root:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
export PATH="$PWD/tools/bin:$PATH"
```

## Project Solver

The project solver is built with QBS from `source/source.qbs`. A clean release build is
created by the acceptance runner:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
python3 benchmarks/run_clean_checkout_gates.py \
  --seed 20260814 \
  --repeat-count 2 \
  --differential-cases 128
```

The resulting CLI is normally located at:

```text
source/build/clean-gate-release/default/kmx-sat.d9e8dc1a/kmx-sat
```

For a focused local QBS build:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat/source
qbs build -f source.qbs \
  -d "$PWD/../tools/qbs-release-build" \
  config:default \
  profile:default \
  config:release
```

The exact QBS profile and build variant can differ between machines. The clean gate is
the canonical reproducible command for this repository.

## CaDiCaL

The complete CaDiCaL source checkout is under `reference/cadical`.

Build it in its default source-local build directory:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat/reference/cadical
./configure
make -j2 cadical
```

The binary and static library are normally produced under the checkout's `build`
directory as:

```text
<cadical-checkout>/build/cadical
<cadical-checkout>/build/libcadical.a
```

Install the standalone solver into the workspace-local prefix:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
mkdir -p tools/bin
cp reference/cadical/build/cadical tools/bin/cadical
./tools/bin/cadical --version
```

The verified local version is CaDiCaL 3.0.1.

## Kissat

The complete Kissat source checkout is under `reference/kissat`.
Kissat's configure script does not accept the `-q` option in this checkout.

Build it with:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat/reference/kissat
./configure
make -j2 kissat
```

The binary and static library are normally produced under the checkout's `build`
directory as:

```text
<kissat-checkout>/build/kissat
<kissat-checkout>/build/libkissat.a
```

Install the standalone solver into the workspace-local prefix:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
mkdir -p tools/bin
cp reference/kissat/build/kissat tools/bin/kissat
./tools/bin/kissat --version
```

The verified local version is Kissat 4.0.4.

## DRAT-TRIM and LRAT-check

The DRAT-TRIM checkout also contains `lrat-check.c`.
It is stored under `tools/src/drat-trim` and is not part of the three SAT solver
comparison binaries, but it is useful for proof validation.

The default Makefile omits the GNU declaration needed for `getc_unlocked` on this
Linux toolchain. Build the two requested checkers explicitly:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat/tools/src/drat-trim
gcc -D_GNU_SOURCE -std=c99 -O2 drat-trim.c -o drat-trim
gcc -D_GNU_SOURCE -DLONGTYPE -std=c99 -O2 lrat-check.c -o lrat-check

cd /home/cflaviu/Development/kmx/kmx-sat
cp tools/src/drat-trim/drat-trim tools/bin/drat-trim
cp tools/src/drat-trim/lrat-check tools/bin/lrat-check
```

## VeriPB

VeriPB is installed in the workspace-local virtual environment:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
python3 -m venv tools/veripb-venv
tools/veripb-venv/bin/python -m pip install --upgrade pip setuptools wheel pybind11
tools/veripb-venv/bin/python -m pip install --no-build-isolation -e tools/src/VeriPB
ln -sf ../veripb-venv/bin/veripb tools/bin/veripb
```

Run it with either command:

```bash
tools/bin/veripb --help
# or
tools/veripb-venv/bin/veripb --help
```

VeriPB requires GMP and builds a C++/pybind11 extension. Keeping it in a virtual
environment avoids system-wide Python changes.

## Verify the Installation

The repository includes a reproducible dependency report:

```bash
cd /home/cflaviu/Development/kmx/kmx-sat
PATH="$PWD/tools/bin:$PATH" \
  python3 benchmarks/generate_external_dependency_report.py \
  --output clean-checkout-gates/external-dependency-report.json
```

The report records all five optional tools:

- `cadical`
- `kissat`
- `drat-trim`
- `lrat-check`
- `veripb`

The installed tools are also listed in [tools/README.md](../tools/README.md).
