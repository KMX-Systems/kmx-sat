import qbs

CppApplication {
    name: "kmx-sat-c-api-smoke"

    Depends { name: "kmx-sat-lib" }

    consoleApplication: true
    files: ["c_api_smoke.c"]
    cpp.cLanguageVersion: "c11"
    cpp.includePaths: ["../library/api"]
    Properties {
        condition: qbs.buildVariant === "release"
        cpp.debugInformation: false
        cpp.defines: ["NDEBUG"]
        cpp.commonCompilerFlags: ["-Ofast", "-march=native", "-flto=auto"]
        // kmx-sat-cdcl and kmx-sat-simplify reference each other's symbols (solver_core
        // drives the simplification schedulers; the schedulers work on cdcl clause
        // storage), which no single order of static archives can satisfy. Link them as
        // one group: cpp.linkerFlags lands before the archives, driverLinkerFlags after.
        cpp.linkerFlags: ["-flto=auto", "--start-group"]
        cpp.driverLinkerFlags: ["-Wl,--end-group"]
    }
}