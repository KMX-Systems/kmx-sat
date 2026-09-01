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
        cpp.driverLinkerFlags: ["-flto=auto"]
    }
    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: "bin"
    }
}
