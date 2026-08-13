import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-io-test"
    testSources: [
        "src/kmx/sat/io/**/*.cpp",
    ]
}