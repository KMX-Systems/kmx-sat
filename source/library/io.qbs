import qbs

StaticLibrary {
    name: "kmx-sat-io"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Depends { name: "kmx-sat-cdcl" }
    Depends { name: "kmx-sat-proof" }
    Depends { name: "kmx-sat-telemetry" }
    Group {
        name: "io"
        prefix: "inc/kmx/sat/io/"
        files: [
            "dimacs_parser.hpp",
            "file_source.hpp",
            "fixture/binary/reader.hpp",
            "fixture/binary/writer.hpp",
            "fixture/manifest.hpp",
            "fixture/schema.hpp",
            "fixture/validator.hpp",
            "proof_output_pipeline.hpp",
            "writer/format.hpp",
            "writer/kmx_aio_proof.hpp",
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
}
