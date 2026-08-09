import qbs

StaticLibrary {
    name: "kmx-sat-runtime"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "runtime"
        prefix: "inc/kmx/sat/runtime/"
        files: [
            "co_fsm_adapter.hpp",
            "parallel_preprocess_executor.hpp",
            "shared_clause_exchange.hpp",
        ]
    }
    Group {
        name: "runtime-controller"
        prefix: "inc/kmx/sat/runtime/controller/"
        files: [
            "portfolio.hpp",
            "signal.hpp",
        ]
    }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: ["api", "inc"]

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-sat-types" }
        cpp.includePaths: ["api", "inc"]
    }
}
