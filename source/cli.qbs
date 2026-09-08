import qbs

CppApplication {
    name: "kmx-sat"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-lib" }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.enableExceptions: true
    cpp.warningLevel: "all"
    files: [
        "cli/kmx-sat-main.cpp",
    ]
    Properties {
        condition: qbs.buildVariant === "release"
        cpp.debugInformation: false
        cpp.optimization: "fast"
        cpp.defines: ["NDEBUG"]
        // -O3 rather than -Ofast: -ffast-math relaxes the floating-point semantics the EMA and clause-activity
        // scores depend on, and measurement showed it buys nothing (identical conflict counts and timings within
        // noise). -march=native is likewise omitted so a release build stays portable and reproducible, which is
        // what the clean-checkout gates claim; pass it explicitly when tuning for one machine.
        cpp.commonCompilerFlags: ["-O3", "-flto=auto"]
        // Statically linked. Every instance in the corpus that finishes in a few milliseconds is dominated by
        // process startup rather than by search, and resolving libstdc++/libgcc/libm through the dynamic loader
        // is most of that: measured over 300 runs, sat_unit.cnf takes 0.90 ms dynamically linked and 0.44 ms
        // statically, against 0.97 ms for CaDiCaL. It costs binary size (0.3 MB -> 3.0 MB) and nothing else here:
        // the CLI reads a file and computes, with no dlopen and no NSS lookups to be caught by static glibc. It
        // also removes the libstdc++ version dependency from the shipped binary, which is the same portability
        // goal that keeps -march=native out of this build.
        // kmx-sat-cdcl and kmx-sat-simplify reference each other's symbols (solver_core
        // drives the simplification schedulers; the schedulers work on cdcl clause
        // storage), which no single order of static archives can satisfy. Link them as
        // one group: cpp.linkerFlags lands before the archives, driverLinkerFlags after.
        cpp.linkerFlags: ["--start-group"]
        cpp.driverLinkerFlags: ["-flto=auto", "-static", "-Wl,--end-group"]
    }
    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: "bin"
    }
}
