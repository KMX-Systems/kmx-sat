import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-io-test"
    testSources: [
        "src/kmx/sat/io/**/*.cpp",
    ]
}