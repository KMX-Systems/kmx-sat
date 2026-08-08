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

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-sat-types" }
        cpp.includePaths: ["api", "inc"]
    }
}
