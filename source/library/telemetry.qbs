import qbs

StaticLibrary {
    name: "kmx-sat-telemetry"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "telemetry-api"
        prefix: "api/kmx/sat/telemetry/"
        files: [
            "solver_options.hpp",
            "solver_statistics.hpp",
        ]
    }
    Group {
        name: "telemetry-inc"
        prefix: "inc/kmx/sat/telemetry/"
        files: [
            "ema_tracker.hpp",
            "logging_facade.hpp",
            "profile_clock.hpp",
            "report_formatter.hpp",
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
