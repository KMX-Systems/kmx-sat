import qbs
import "." as TestItems

TestItems.TestApplication {
    name: "kmx-sat-cdcl-test"
    testSources: [
        "src/kmx/sat/cdcl/**/*.cpp",
    ]
    extraSources: [
        "src/kmx/sat/solver_state_machine_test.cpp",
    ]
}