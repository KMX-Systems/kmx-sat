import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-runtime-test"
    testSources: [
        "src/kmx/sat/runtime/**/*.cpp",
    ]
}