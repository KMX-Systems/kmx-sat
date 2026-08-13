import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-types-test"
    testSources: [
        "src/kmx/sat/types/**/*.cpp",
    ]
}