import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-types-test"
    testSources: [
        "src/kmx/sat/types/**/*.cpp",
    ]
}