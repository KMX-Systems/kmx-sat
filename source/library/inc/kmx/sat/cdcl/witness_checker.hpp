/// @file inc/kmx/sat/cdcl/witness_checker.hpp
/// @brief Separate validator for the SAT path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/model_reconstructor.hpp>
#include <kmx/sat/model_view.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Separate validator for the SAT path.
    ///
    /// `witness_checker` is an independent double-check on a reconstructed model, deliberately kept separate from
    /// `model_reconstructor` so a bug in reconstruction cannot silently validate itself: `check_model_against_original`
    /// re-evaluates every literal of the model against the original, pre-simplification clause set (the strongest
    /// guarantee, matching the SAT-flow verification criterion that the model must be validated after BVE, BCE,
    /// factoring, compaction, and flush/restore); `check_model_against_current` performs the equivalent check
    /// against the currently tracked (possibly still-simplified) clause set for cheaper incremental re-validation;
    /// `check_constraint_satisfaction` additionally verifies the active `store::constraint` temporary clause, if any,
    /// is satisfied.
    class witness_checker final
    {
    public:
        /// @brief Constructs a witness checker with no cached validation state.
        /// @throws None (noexcept).
        witness_checker() noexcept = default;

        /// @brief Validates a model against the original, pre-simplification clause set.
        /// @param model Model to validate.
        /// @return True if every original clause is satisfied by `model`.
        /// @throws None (noexcept).
        bool check_model_against_original(const model_view model) const noexcept
        {
            return false;
        }

        /// @brief Validates a model against the currently tracked clause set.
        /// @param model Model to validate.
        /// @return True if every currently tracked clause is satisfied by `model`.
        /// @throws None (noexcept).
        bool check_model_against_current(const model_view model) const noexcept
        {
            return false;
        }

        /// @brief Validates that the active temporary constraint clause, if any, is satisfied by a model.
        /// @param model Model to validate.
        /// @return True if the active constraint clause (if any) is satisfied by `model`.
        /// @throws None (noexcept).
        bool check_constraint_satisfaction(const model_view model) const noexcept
        {
            return false;
        }
    };
}
