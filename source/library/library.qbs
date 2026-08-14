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
            "ipasir.h",
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
    cpp.warningLevel: "all"
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
        cpp.commonCompilerFlags: ["-Ofast", "-march=native", "-flto=auto"]
        cpp.linkerFlags: ["-flto=auto"]
        cpp.defines: ["NDEBUG"]
    }
    Properties {
        condition: qbs.buildVariant === "debug"
        cpp.defines: ["ASAN_OPTIONS=abort_on_error=1:report_objects=1:sleep_before_dying=1"]
        cpp.cxxFlags: "-fsanitize=address"
        cpp.staticLibraries: "asan"
    }
}
