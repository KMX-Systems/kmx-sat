import qbs

StaticLibrary {
    name: "kmx-sat-proof"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "proof"
        prefix: "inc/kmx/sat/proof/"
        files: [
            "event_stream.hpp"
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
        Depends { name: "kmx-sat-types" }
        cpp.includePaths: ["api", "inc"]
    }
}
