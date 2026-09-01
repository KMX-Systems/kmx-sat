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
            "proof_output_pipeline.hpp",
        ]
    }
    Group {
        name: "io-writer"
        prefix: "inc/kmx/sat/io/writer/"
        files: [
            "format.hpp",
            "kmx_aio_proof.hpp",
        ]
    }
    Group {
        name: "io-fixture"
        prefix: "inc/kmx/sat/io/fixture/"
        files: [
            "manifest.hpp",
            "schema.hpp",
            "validator.hpp",
        ]
    }
    Group {
        name: "io-fixture-binary"
        prefix: "inc/kmx/sat/io/fixture/binary/"
        files: [
            "reader.hpp",
            "writer.hpp",
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
        Depends { name: "kmx-sat-cdcl" }
        Depends { name: "kmx-sat-proof" }
        Depends { name: "kmx-sat-telemetry" }
        cpp.includePaths: ["api", "inc"]
    }
}
