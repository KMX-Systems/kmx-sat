import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-telemetry-test"
    testSources: [
        "src/kmx/sat/telemetry/**/*.cpp",
    ]
}