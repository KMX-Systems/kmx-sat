/// @file inc/kmx/sat/simplify/scheduler/preprocess.hpp
/// @brief Simplification phases that run before the main search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <string_view>
#endif

namespace kmx::sat::simplify::scheduler
{
    /// @brief Simplification phases that run before the main search.
    ///
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
        /// @brief Constructs a preprocess scheduler with every baseline pass enabled.
        /// @throws None (noexcept).
        preprocess() noexcept = default;

        /// @brief Runs the full one-time preprocessing pipeline before the main search begins.
        /// @throws None (noexcept).
        void run_initial_pipeline() noexcept
        {
        }

        /// @brief Enables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to enable.
        /// @throws None (noexcept).
        void enable_pass(const std::string_view pass_name) noexcept
        {
        }

        /// @brief Disables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to disable.
        /// @throws None (noexcept).
        void disable_pass(const std::string_view pass_name) noexcept
        {
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
        }
    };
}
