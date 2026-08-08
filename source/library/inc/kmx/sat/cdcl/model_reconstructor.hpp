/// @file inc/kmx/sat/cdcl/model_reconstructor.hpp
/// @brief Reconstructs the external model after eliminations and compaction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/model_view.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Reconstructs the external model after eliminations and compaction.
    ///
    /// After `solver_core::extract_internal_model` produces a satisfying assignment over the reduced (post-BVE/
    /// BCE/factoring) internal variable set, `model_reconstructor` walks `stack::extension` in reverse
    /// (`reconstruct_full_model` calling `apply_extension_record` per entry) to derive correct values for every
    /// eliminated or blocked variable, restoring a full model over the original problem's variables.
    /// `drop_internal_only_variables` removes internally introduced variables (from `factorizer`/`gate_extractor`)
    /// that must never be exposed externally, per `variable_mapper`'s disjoint internal-variable range.
    /// `validate_clause_satisfaction` provides a cheap self-check that the reconstructed model satisfies the
    /// currently tracked clause set, ahead of the fuller independent check performed by `witness_checker`.
    /// @note This class is the single authority for filtering internal-only variables out of any model exposed
    /// through `model_view`; no other component may bypass it to expose a model directly from `solver_core`.
    class model_reconstructor final
    {
    public:
        /// @brief Constructs a model reconstructor with an empty extension-stack binding.
        /// @throws None (noexcept).
        model_reconstructor() noexcept = default;

        /// @brief Reconstructs the full external model by replaying the extension stack in reverse order.
        /// @return Read-only view over the reconstructed external model.
        /// @throws None (noexcept).
        model_view reconstruct_full_model() noexcept
        {
            return {};
        }

        /// @brief Applies one extension-stack record's reversal to the in-progress reconstructed model.
        /// @param record Extension record to apply.
        /// @throws None (noexcept).
        void apply_extension_record(const extension_record& record) noexcept
        {
        }

        /// @brief Removes internal-only variables from the reconstructed model before external exposure.
        /// @throws None (noexcept).
        void drop_internal_only_variables() noexcept
        {
        }

        /// @brief Performs a cheap self-check that the reconstructed model satisfies the tracked clause set.
        /// @return True if the reconstructed model satisfies every checked clause.
        /// @throws None (noexcept).
        bool validate_clause_satisfaction() const noexcept
        {
            return false;
        }
    };
}
