import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-simplify-test"
    testSources: [
        "src/kmx/sat/simplify/**/*.cpp",
    ]
}