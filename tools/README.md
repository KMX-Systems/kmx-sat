# Local SAT Tools

The requested external tools are installed in `tools/bin`:

- `cadical` 3.0.1
- `kissat` 4.0.4
- `drat-trim`
- `lrat-check` (bundled by the DRAT-TRIM checkout)
- `veripb` 0.3a0

Use them from the repository root with:

```text
export PATH="$PWD/tools/bin:$PATH"
```

Build/source locations:

- CaDiCaL: `reference/cadical/build/cadical`
- Kissat: `reference/kissat/build/kissat`
- DRAT-TRIM and LRAT-check: `tools/src/drat-trim`
- VeriPB source and virtual environment: `tools/src/VeriPB` and `tools/veripb-venv`

The DRAT-TRIM checkout's default Makefile omits GNU feature declarations on this machine, so its two requested binaries were compiled with `-D_GNU_SOURCE`. VeriPB was installed into the local virtual environment with pybind11 and Cython; no system-wide packages or `/usr/local` writes were used.
