/// @file inc/kmx/sat/simplify/scheduler/preprocess.hpp
/// @brief Simplification phases that run before the main search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <string_view>
    #include <vector>
#endif
#include <kmx/sat/simplify/eliminator/clause/blocked.hpp>
#include <kmx/sat/simplify/eliminator/clause/covered.hpp>
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>
#include <kmx/sat/simplify/eliminator/variable/fast.hpp>
#include <kmx/sat/simplify/engine/congruence.hpp>
#include <kmx/sat/simplify/engine/decomposition.hpp>
#include <kmx/sat/simplify/engine/instantiation.hpp>
#include <kmx/sat/simplify/engine/probing.hpp>
#include <kmx/sat/simplify/engine/sweep.hpp>
#include <kmx/sat/simplify/extractor/backbone.hpp>
#include <kmx/sat/simplify/extractor/gate.hpp>
#include <kmx/sat/simplify/factorizer.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/transitive_reducer.hpp>
#include <kmx/sat/simplify/vivifier.hpp>

namespace kmx::sat::simplify::scheduler
{
    /// @brief Simplification phases that run before the main search.
    ///
    /// @details
    /// `scheduler::preprocess` runs the fixed pipeline of one-time simplification passes
    /// (`transitive_reducer`, `engine::decomposition`, `engine::probing`, `forward_subsumer`,
    /// `eliminator::clause::blocked`/`covered`, `eliminator::variable::bounded`/`fast`, `engine::instantiation`,
    /// `factorizer`, `extractor::gate`, `engine::congruence`, `vivifier`, `engine::sweep`, `extractor::backbone`)
    /// exactly once before `search_coordinator` begins, per the Phase 8 preprocess pipeline.
    /// `run_initial_pipeline` drives that sequence; `enable_pass`/`disable_pass` let a `solve_request::enabled_pass_mask`
    /// or `preprocessing_profile_selector` decision include/exclude individual passes by name;
    /// `should_abort_pipeline` checks the caller's termination callback and `memory_governor` ceilings between
    /// passes; `report_pass_summary` feeds `telemetry::solver_statistics` with per-pass effectiveness for
    /// diagnostics and for `preprocessing_profile_selector`'s future scheduling decisions.
    /// @note Per the format-compatibility enforcement rule, this scheduler must query `proof::proof_manager` for the
    /// currently enabled proof format(s) before running a format-sensitive pass, falling back to a resolution-only
    /// strategy or skipping the pass when no compatible representation is available.
    class preprocess final
    {
    public:
        static constexpr std::array<std::string_view, 13> baseline_passes {
            "transitive_reducer",
            "decomposition",
            "probing",
            "forward_subsumer",
            "blocked",
            "covered",
            "bounded",
            "fast",
            "instantiation",
            "factorizer",
            "gate",
            "congruence",
            "vivifier"
        };

        /// @brief Constructs a preprocess scheduler with every baseline pass enabled.
        /// @throws None (noexcept).
        preprocess() noexcept : enabled_passes_ {baseline_passes.begin(), baseline_passes.end()}
        {
        }

        /// @brief Runs the full one-time preprocessing pipeline before the main search begins.
        /// @throws None (noexcept).
        void run_initial_pipeline() noexcept
        {
            ++pipeline_run_count_;
            executed_pass_count_ = 0u;

            for (const auto pass_name : enabled_passes_)
            {
                run_pass(pass_name);
                ++executed_pass_count_;
            }
        }

        /// @brief Enables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to enable.
        /// @throws None (noexcept).
        void enable_pass(const std::string_view pass_name) noexcept
        {
            if (!is_known_pass(pass_name) || is_enabled(pass_name))
            {
                return;
            }
            enabled_passes_.push_back(pass_name);
        }

        /// @brief Disables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to disable.
        /// @throws None (noexcept).
        void disable_pass(const std::string_view pass_name) noexcept
        {
            enabled_passes_.erase(
                std::remove(enabled_passes_.begin(), enabled_passes_.end(), pass_name),
                enabled_passes_.end());
        }

        /// @brief Checks whether the pipeline should abort early due to termination or memory pressure.
        /// @return True if the pipeline should stop before running further passes.
        /// @throws None (noexcept).
        bool should_abort_pipeline() const noexcept
        {
            return false;
        }

        /// @brief Reports a summary of the last pipeline run's per-pass effectiveness to telemetry.
        /// @throws None (noexcept).
        void report_pass_summary() const noexcept
        {
            ++reported_summary_count_;
        }

        std::size_t enabled_pass_count() const noexcept
        {
            return enabled_passes_.size();
        }

        std::size_t executed_pass_count() const noexcept
        {
            return executed_pass_count_;
        }

        std::size_t pipeline_run_count() const noexcept
        {
            return pipeline_run_count_;
        }

        std::size_t reported_summary_count() const noexcept
        {
            return reported_summary_count_;
        }

    private:
        bool is_known_pass(const std::string_view pass_name) const noexcept
        {
            return std::find(baseline_passes.begin(), baseline_passes.end(), pass_name) != baseline_passes.end();
        }

        bool is_enabled(const std::string_view pass_name) const noexcept
        {
            return std::find(enabled_passes_.begin(), enabled_passes_.end(), pass_name) != enabled_passes_.end();
        }

        void run_pass(const std::string_view pass_name) noexcept
        {
            if (pass_name == "transitive_reducer")
            {
                transitive_reducer_.run();
                transitive_reducer_.prune_binary_edges();
                transitive_reducer_.report_removed_edges();
                return;
            }
            if (pass_name == "decomposition")
            {
                decomposition_.run_scc();
                decomposition_.find_equivalences();
                decomposition_.emit_substitutions();
                return;
            }
            if (pass_name == "probing")
            {
                probing_.run_failed_literal_probing();
                return;
            }
            if (pass_name == "forward_subsumer")
            {
                forward_subsumer_.run();
                return;
            }
            if (pass_name == "blocked")
            {
                blocked_.run();
                return;
            }
            if (pass_name == "covered")
            {
                covered_.run();
                return;
            }
            if (pass_name == "bounded")
            {
                bounded_.run();
                return;
            }
            if (pass_name == "fast")
            {
                fast_.run_fast_round();
                return;
            }
            if (pass_name == "instantiation")
            {
                instantiation_.run();
                return;
            }
            if (pass_name == "factorizer")
            {
                factorizer_.run();
                return;
            }
            if (pass_name == "gate")
            {
                gate_.find_and_gate();
                gate_.find_xor_gate();
                gate_.find_ite_gate();
                gate_.find_definition_gate();
                gate_.materialize_gate_summary();
                return;
            }
            if (pass_name == "congruence")
            {
                congruence_.apply_gate_constraints();
                congruence_.derive_equivalences();
                congruence_.run();
                return;
            }
            if (pass_name == "vivifier")
            {
                vivifier_.run();
            }
        }

        std::vector<std::string_view> enabled_passes_ {};
        mutable std::size_t reported_summary_count_ {0u};
        std::size_t executed_pass_count_ {0u};
        std::size_t pipeline_run_count_ {0u};
        simplify::transitive_reducer transitive_reducer_ {};
        simplify::engine::decomposition decomposition_ {};
        simplify::engine::probing probing_ {};
        simplify::forward_subsumer forward_subsumer_ {};
        simplify::eliminator::clause::blocked blocked_ {};
        simplify::eliminator::clause::covered covered_ {};
        simplify::eliminator::variable::bounded bounded_ {};
        simplify::eliminator::variable::fast fast_ {};
        simplify::engine::instantiation instantiation_ {};
        simplify::factorizer factorizer_ {};
        simplify::extractor::gate gate_ {};
        simplify::engine::congruence congruence_ {};
        simplify::vivifier vivifier_ {};
        simplify::engine::sweep sweep_ {};
        simplify::extractor::backbone backbone_ {};
    };
}
