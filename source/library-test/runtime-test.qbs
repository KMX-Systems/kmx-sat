import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-runtime-test"
    testSources: [
        "src/kmx/sat/runtime/**/*.cpp",
    ]
}