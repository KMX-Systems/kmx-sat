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
            "bank/arena.hpp",
            "bank/watch_list.hpp",
            "chb_tracker.hpp",
            "clause/database.hpp",
            "clause/header.hpp",
            "clause/learner.hpp",
            "clause/minimizer.hpp",
            "clause/storage.hpp",
            "clause/view.hpp",
            "compaction_service.hpp",
            "conflict_analyzer.hpp",
            "controller/reduce.hpp",
            "controller/rephase.hpp",
            "controller/restart.hpp",
            "engine/backtrack.hpp",
            "engine/decision.hpp",
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
            "stack/decision_frame.hpp",
            "stack/extension.hpp",
            "store/assignment.hpp",
            "store/assumption.hpp",
            "store/clause_cold.hpp",
            "store/constraint.hpp",
            "store/phase.hpp",
            "trail.hpp",
            "variable_mapper.hpp",
            "vmtf_queue.hpp",
            "watch.hpp",
            "witness_checker.hpp",
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

    Export {
        Depends { name: "cpp" }
        Depends { name: "kmx-sat-types" }
        Depends { name: "kmx-sat-telemetry" }
        cpp.includePaths: ["api", "inc"]
    }
}
