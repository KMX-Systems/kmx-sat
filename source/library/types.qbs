import qbs

StaticLibrary {
    name: "kmx-sat-types"
    Depends { name: "cpp" }
    // Shared leaf types with no cross-subsystem includes; hosting cdcl::clause::ref_t and
    // proof::clause::id here is what breaks the cdcl <-> proof header cycle.
    Group {
        name: "types-api"
        prefix: "api/kmx/sat/"
        files: [
            "failed_core_view.hpp",
            "literal.hpp",
            "model_view.hpp",
            "solve_request.hpp",
            "variable.hpp",
        ]
    }
    Group {
        name: "types-inc"
        prefix: "inc/kmx/sat/"
        files: [
            "cdcl/clause/ref_t.hpp",
            "proof/clause/id.hpp",
        ]
    }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: ["api", "inc"]

    Properties {
        condition: qbs.buildVariant === "release"
        cpp.commonCompilerFlags: ["-Ofast", "-march=native", "-flto=auto"]
        cpp.linkerFlags: ["-flto=auto"]
        cpp.defines: ["NDEBUG"]
    }

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: ["api", "inc"]
    }
}
