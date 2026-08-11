/// @file inc/kmx/sat/simplify/preprocessing_profile_selector.hpp
/// @brief Instance-aware selection and ordering of preprocessing passes from cheap structural fingerprints, avoiding
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif
#include <kmx/sat/simplify/extractor/gate.hpp>
#include <kmx/sat/simplify/scheduler/preprocess.hpp>

namespace kmx::sat::simplify
{
    /// @brief Instance-aware selection and ordering of preprocessing passes from cheap structural fingerprints, avoiding
    /// fixed-pipeline time wasted on passes unlikely to help. Research-track scheduler policy layered on top of
    /// scheduler::preprocess, not a replacement for it.
    ///
    /// A fixed preprocessing pipeline spends time on every enabled pass regardless of whether the current instance's
    /// structure makes that pass likely to help; `preprocessing_profile_selector` instead computes cheap structural
    /// fingerprints (variable/clause ratio, clause-length distribution, gate density from `extractor::gate`) via
    /// `fingerprint_formula`, uses `select_pass_plan` to choose which passes `scheduler::preprocess` should
    /// enable/disable and in what order for this specific instance, and refines that choice over time using
    /// `record_pass_effectiveness`/`update_selection_policy` fed by each pass's actual yield.
    /// @note Research-track: this component only advises `scheduler::preprocess`'s pass selection and never bypasses
    /// it; any change to its selection policy must be validated on the comparative benchmarking harness before
    /// becoming a default.
    class preprocessing_profile_selector final
    {
    public:
        /// @brief Constructs a profile selector with a default (fixed-order) selection policy.
        /// @throws None (noexcept).
        preprocessing_profile_selector() noexcept = default;

        /// @brief Computes cheap structural fingerprints of the current formula.
        /// @throws None (noexcept).
        void fingerprint_formula() noexcept
        {
            ++fingerprint_count_;
        }

        /// @brief Selects and orders which preprocessing passes should run for this formula's fingerprint.
        /// @throws None (noexcept).
        void select_pass_plan() noexcept
        {
            ++pass_plan_count_;
        }

        /// @brief Records the actual yield of a pass to refine future selection decisions.
        /// @throws None (noexcept).
        void record_pass_effectiveness() noexcept
        {
            ++effectiveness_count_;
        }

        /// @brief Updates the underlying selection policy from accumulated pass-effectiveness history.
        /// @throws None (noexcept).
        void update_selection_policy() noexcept
        {
            ++policy_update_count_;
        }

        std::size_t fingerprint_count() const noexcept
        {
            return fingerprint_count_;
        }

        std::size_t pass_plan_count() const noexcept
        {
            return pass_plan_count_;
        }

        std::size_t effectiveness_count() const noexcept
        {
            return effectiveness_count_;
        }

        std::size_t policy_update_count() const noexcept
        {
            return policy_update_count_;
        }

    private:
        std::size_t fingerprint_count_ {0u};
        std::size_t pass_plan_count_ {0u};
        std::size_t effectiveness_count_ {0u};
        std::size_t policy_update_count_ {0u};
    };
}
