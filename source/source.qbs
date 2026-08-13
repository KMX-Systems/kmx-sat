import qbs 1.0

Project {
    references: [
        "library/types.qbs",
        "library/telemetry.qbs",
        "library/cdcl.qbs",
        "library/proof.qbs",
        "library/simplify.qbs",
        "library/runtime.qbs",
        "library/io.qbs",
        "library/library.qbs",
        "library-test/unit-test.qbs",
        "cli.qbs"
    ]
}
