import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-lib-test"
    extraSources: [
        "src/kmx/sat/solver_facade_test.cpp",
    ]
}