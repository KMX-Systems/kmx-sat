import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-proof-test"
    testSources: [
        "src/kmx/sat/proof/**/*.cpp",
    ]
}