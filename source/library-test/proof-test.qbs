import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-proof-test"
    testSources: [
        "src/kmx/sat/proof/**/*.cpp",
    ]
}