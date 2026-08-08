import qbs

StaticLibrary {
    name: "kmx-sat-proof"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "proof"
        prefix: "inc/kmx/sat/proof/"
        files: [
            "checker/lrat.hpp",
            "checker/online.hpp",
            "clause/id_allocator.hpp",
            "event_stream.hpp",
            "tracer/drat.hpp",
            "tracer/frat.hpp",
            "tracer/idrup.hpp",
            "tracer/lidrup.hpp",
            "tracer/like.hpp",
            "tracer/lrat.hpp",
            "tracer/variant_t.hpp",
            "tracer/veripb.hpp",
            "tracer/view.hpp",
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
