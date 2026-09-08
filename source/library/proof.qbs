import qbs

StaticLibrary {
    name: "kmx-sat-proof"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "proof"
        prefix: "inc/kmx/sat/proof/"
        files: [
            "event_stream.hpp",
            "format.hpp"
        ]
    }
    Group {
        name: "proof-clause"
        prefix: "inc/kmx/sat/proof/clause/"
        files: [
            "id.hpp",
            "id_allocator.hpp"
        ]
    }
    Group {
        name: "proof-checker"
        prefix: "inc/kmx/sat/proof/checker/"
        files: [
            "lrat.hpp",
            "online.hpp"
        ]
    }
    Group {
        name: "proof-tracer"
        prefix: "inc/kmx/sat/proof/tracer/"
        files: [
            "drat.hpp",
            "frat.hpp",
            "idrup.hpp",
            "lidrup.hpp",
            "like.hpp",
            "lrat.hpp",
            "variant_t.hpp",
            "veripb.hpp",
            "view.hpp",
        ]
    }
    Group {
        name: "src-proof"
        prefix: "src/kmx/sat/proof/"
        files: [
            "event_stream.cpp",
        ]
    }
    Group {
        name: "src-proof-checker"
        prefix: "src/kmx/sat/proof/checker/"
        files: [
            "lrat.cpp",
            "online.cpp",
        ]
    }
    Group {
        name: "src-proof-clause"
        prefix: "src/kmx/sat/proof/clause/"
        files: [
            "id_allocator.cpp",
        ]
    }
    Group {
        name: "src-proof-tracer"
        prefix: "src/kmx/sat/proof/tracer/"
        files: [
            "drat.cpp",
            "frat.cpp",
            "idrup.cpp",
            "lidrup.cpp",
            "lrat.cpp",
            "veripb.cpp",
            "view.cpp",
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
