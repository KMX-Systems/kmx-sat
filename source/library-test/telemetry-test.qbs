import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-telemetry-test"
    testSources: [
        "src/kmx/sat/telemetry/**/*.cpp",
    ]
}