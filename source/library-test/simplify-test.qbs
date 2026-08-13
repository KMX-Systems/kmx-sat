import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-simplify-test"
    testSources: [
        "src/kmx/sat/simplify/**/*.cpp",
    ]
}