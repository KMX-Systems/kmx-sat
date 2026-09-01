import qbs

StaticLibrary {
    name: "kmx-sat-cdcl"
    Depends { name: "cpp" }
    Depends { name: "kmx-sat-types" }
    Depends { name: "kmx-sat-telemetry" }
    Group {
        name: "cdcl"
        prefix: "inc/kmx/sat/cdcl/"
        files: [
            "assumption_reuse_advisor.hpp",
            "chb_tracker.hpp",
            "compaction_service.hpp",
            "conflict_analyzer.hpp",
            "evsids_heap.hpp",
            "extension_record.hpp",
            "external_frontend.hpp",
            "failed_core_extractor.hpp",
            "garbage_collector.hpp",
            "incremental_context.hpp",
            "memory_governor.hpp",
            "model_reconstructor.hpp",
            "otf_strengthener.hpp",
            "propagator.hpp",
            "search_coordinator.hpp",
            "solver_core.hpp",
            "trail.hpp",
            "variable_mapper.hpp",
            "vmtf_queue.hpp",
            "watch.hpp",
            "witness_checker.hpp",
        ]
    }
    Group {
        name: "cdcl-bank"
        prefix: "inc/kmx/sat/cdcl/bank/"
        files: [
            "arena.hpp",
            "watch_list.hpp",
        ]
    }
    Group {
        name: "cdcl-clause"
        prefix: "inc/kmx/sat/cdcl/clause/"
        files: [
            "database.hpp",
            "header.hpp",
            "learner.hpp",
            "minimizer.hpp",
            "storage.hpp",
            "view.hpp",
        ]
    }
    Group {
        name: "cdcl-controller"
        prefix: "inc/kmx/sat/cdcl/controller/"
        files: [
            "reduce.hpp",
            "rephase.hpp",
            "restart.hpp",
        ]
    }
    Group {
        name: "cdcl-engine"
        prefix: "inc/kmx/sat/cdcl/engine/"
        files: [
            "backtrack.hpp",
            "decision.hpp",
        ]
    }
    Group {
        name: "cdcl-stack"
        prefix: "inc/kmx/sat/cdcl/stack/"
        files: [
            "decision_frame.hpp",
            "extension.hpp",
        ]
    }
    Group {
        name: "cdcl-store"
        prefix: "inc/kmx/sat/cdcl/store/"
        files: [
            "assignment.hpp",
            "assumption.hpp",
            "clause_cold.hpp",
            "constraint.hpp",
            "phase.hpp",
        ]
    }
    Group {
        name: "sat"
        prefix: "inc/kmx/sat/"
        files: [
            "solver_state_machine.hpp",
        ]
    }
    cpp.cxxLanguageVersion: "c++26"
    cpp.enableRtti: false
    cpp.includePaths: ["api", "inc"]

    Properties {
        condition: qbs.buildVariant === "release"
        // -O3 rather than -Ofast: -ffast-math relaxes the floating-point semantics the EMA and clause-activity
        // scores depend on, and measurement showed it buys nothing (identical conflict counts and timings within
        // noise). -march=native is likewise omitted so a release build stays portable and reproducible, which is
        // what the clean-checkout gates claim; pass it explicitly when tuning for one machine.
        cpp.commonCompilerFlags: ["-O3", "-flto=auto"]
        cpp.driverLinkerFlags: ["-flto=auto"]
        cpp.defines: ["NDEBUG"]
    }

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-sat-types" }
        Depends { name: "kmx-sat-telemetry" }
        cpp.includePaths: ["api", "inc"]
    }
}
