import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-cdcl-test"
    testSources: [
        "src/kmx/sat/cdcl/**/*.cpp",
    ]
    extraSources: [
        "src/kmx/sat/solver_state_machine_test.cpp",
    ]
}