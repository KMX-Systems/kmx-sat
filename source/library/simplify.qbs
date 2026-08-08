import qbs

StaticLibrary {
    name: "kmx-sat-simplify"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "simplify"
        prefix: "inc/kmx/sat/simplify/"
        files: [
            "eliminator/clause/blocked.hpp",
            "eliminator/clause/covered.hpp",
            "eliminator/variable/bounded.hpp",
            "eliminator/variable/fast.hpp",
            "engine/congruence.hpp",
            "engine/decomposition.hpp",
            "engine/instantiation.hpp",
            "engine/probing.hpp",
            "engine/sweep.hpp",
            "equivalence_substitutor.hpp",
            "extractor/backbone.hpp",
            "extractor/gate.hpp",
            "factorizer.hpp",
            "flush_restore_manager.hpp",
            "forward_subsumer.hpp",
            "preprocessing_profile_selector.hpp",
            "scheduler/inprocess.hpp",
            "scheduler/preprocess.hpp",
            "transitive_reducer.hpp",
            "vivifier.hpp",
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
