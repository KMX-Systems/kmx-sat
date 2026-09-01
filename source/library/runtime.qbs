import qbs

StaticLibrary {
    name: "kmx-sat-runtime"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "runtime"
        prefix: "inc/kmx/sat/runtime/"
        files: [
            "co_fsm_adapter.hpp",
            "parallel_preprocess_executor.hpp",
            "shared_clause_exchange.hpp",
        ]
    }
    Group {
        name: "runtime-controller"
        prefix: "inc/kmx/sat/runtime/controller/"
        files: [
            "portfolio.hpp",
            "signal.hpp",
        ]
    }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: ["api", "inc"]

    Properties {
        condition: qbs.buildVariant === "release"
        // -O3 rather than -Ofast: -ffast-math relaxes the floating-point semantics the EMA and clause-activity
        // scores depend on, and measurement showed it buys nothing (identical conflict counts and timings within
        // noise). -march=native is likewise omitted so a release build stays portable and reproducible, which is
        // what the clean-checkout gates claim; pass it explicitly when tuning for one machine.
        cpp.commonCompilerFlags: ["-O3", "-flto=auto"]
        cpp.driverLinkerFlags: ["-flto=auto"]
        cpp.defines: ["NDEBUG"]
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-sat-types" }
        cpp.includePaths: ["api", "inc"]
    }
}
