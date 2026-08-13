import qbs
import "test-application.qbs" as TestItems

TestItems {
    name: "kmx-sat-lib-test"
    extraSources: [
        "src/kmx/sat/solver_facade_test.cpp",
        "src/kmx/sat/long_incremental_session_test.cpp",
        "src/kmx/sat/randomized_incremental_replay_test.cpp",
        "src/kmx/sat/incremental_replay_trace_test.cpp",
        "src/kmx/sat/long_session_memory_growth_test.cpp",
    ]
}