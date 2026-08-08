import qbs

StaticLibrary {
    name: "kmx-sat-lib"
    Depends {
        name: "cpp"
    }
    Depends { name: "kmx-sat-types" }
    Depends { name: "kmx-sat-cdcl" }
    Depends { name: "kmx-sat-proof" }
    Depends { name: "kmx-sat-telemetry" }
    Group {
        name: "kmx-api"
        prefix: "api/kmx/"
        files: [
            "logger.hpp",
        ]
    }
    Group {
        name: "kmx-sat-api"
        prefix: "api/kmx/sat/"
        files: [
            "c_api_adapter.hpp",
            "proof_manager.hpp",
            "solve_result.hpp",
            "solver.hpp",
        ]
    }
    Group {
        name: "kmx-sat-src"
        prefix: "src/kmx/sat/"
        files: [
            "c_api_adapter.cpp",
            "solver.cpp",
        ]
    }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: ["api", "inc"]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-sat-types" }
        Depends { name: "kmx-sat-cdcl" }
        Depends { name: "kmx-sat-proof" }
        Depends { name: "kmx-sat-telemetry" }
        cpp.includePaths: ["api", "inc"]
    }

    Properties {
        condition: qbs.buildVariant === "release"
        cpp.cxxFlags: ["-Ofast"]
    }
    Properties {
        condition: qbs.buildVariant === "debug"
        cpp.defines: ["ASAN_OPTIONS=abort_on_error=1:report_objects=1:sleep_before_dying=1"]
        cpp.cxxFlags: "-fsanitize=address"
        cpp.staticLibraries: "asan"
    }
}
