import qbs

StaticLibrary {
    name: "kmx-sat-simplify"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Group {
        name: "simplify"
        prefix: "inc/kmx/sat/simplify/"
        files: [
            "equivalence_substitutor.hpp",
            "factorizer.hpp",
            "flush_restore_manager.hpp",
            "forward_subsumer.hpp",
            "preprocessing_profile_selector.hpp",
            "transitive_reducer.hpp",
            "vivifier.hpp",
        ]
    }
    Group {
        name: "simplify-eliminator-clause"
        prefix: "inc/kmx/sat/simplify/eliminator/clause/"
        files: [
            "blocked.hpp",
            "covered.hpp",
        ]
    }
    Group {
        name: "simplify-eliminator-variable"
        prefix: "inc/kmx/sat/simplify/eliminator/variable/"
        files: [
            "bounded.hpp",
            "fast.hpp",
        ]
    }
    Group {
        name: "simplify-engine"
        prefix: "inc/kmx/sat/simplify/engine/"
        files: [
            "congruence.hpp",
            "decomposition.hpp",
            "instantiation.hpp",
            "probing.hpp",
            "sweep.hpp",
        ]
    }
    Group {
        name: "simplify-extractor"
        prefix: "inc/kmx/sat/simplify/extractor/"
        files: [
            "backbone.hpp",
            "gate.hpp",
        ]
    }
    Group {
        name: "simplify-scheduler"
        prefix: "inc/kmx/sat/simplify/scheduler/"
        files: [
            "inprocess.hpp",
            "preprocess.hpp",
        ]
    }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: ["api", "inc"]

    Properties {
        condition: qbs.buildVariant === "release"
        cpp.commonCompilerFlags: ["-Ofast", "-march=native", "-flto=auto"]
        cpp.linkerFlags: ["-flto=auto"]
        cpp.defines: ["NDEBUG"]
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-sat-types" }
        cpp.includePaths: ["api", "inc"]
    }
}
