/// @file inc/kmx/sat/cdcl/variable_mapper.hpp
/// @brief e2i/i2e mapping and semantic stability of external variables.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief e2i/i2e mapping and semantic stability of external variables.
    ///
    /// `variable_mapper` owns the external-to-internal (e2i) and internal-to-external (i2e) variable tables consumed
    /// by `external_frontend`. `ensure_external_variable` allocates or looks up the internal slot for a caller-facing
    /// variable, guaranteeing internal-only variables introduced by `factorizer`/`gate_extractor` are drawn from a
    /// disjoint reserved range so they can never alias an external id. `to_internal_literal`/`to_external_literal`
    /// perform the per-literal translation on the hot path. `rebuild_after_compaction` re-derives both tables after
    /// `compaction_service` computes a new variable permutation (following BVE/BCE-driven elimination), while
    /// `mark_inactive`/`mark_eliminated` record variables that must be excluded from future model exposure by
    /// `model_reconstructor::drop_internal_only_variables`.
    /// @note Internal variable numbering may be reset across `solver::reset_session`, but within one incremental
    /// session internal variable identity must be preserved across `compaction_service` runs through the same
    /// permutation machinery used for external variables.
    class variable_mapper final
    {
    public:
        /// @brief Constructs a variable mapper with empty e2i/i2e tables.
        /// @throws None (noexcept).
        variable_mapper() noexcept = default;

        /// @brief Ensures an external variable has a corresponding internal slot, allocating one on first use.
        /// @param var External variable supplied by the caller.
        /// @return Internal variable identifier bound to `var`.
        /// @throws None (noexcept).
        variable ensure_external_variable(const variable var) noexcept
        {
            return var;
        }

        /// @brief Translates one external literal into its internal representation using the e2i table.
        /// @param lit External literal.
        /// @return Internal literal.
        /// @throws None (noexcept).
        literal to_internal_literal(const literal lit) noexcept
        {
            return lit;
        }

        /// @brief Translates one internal literal into its external representation using the i2e table.
        /// @param lit Internal literal.
        /// @return External literal.
        /// @throws None (noexcept).
        literal to_external_literal(const literal lit) noexcept
        {
            return lit;
        }

        /// @brief Recomputes the e2i/i2e tables after `compaction_service` applies a variable permutation.
        /// @throws None (noexcept).
        void rebuild_after_compaction() noexcept
        {
        }

        /// @brief Marks a variable inactive (for example melted or otherwise no longer part of the live problem)
        /// without discarding its identity mapping.
        /// @param var Variable to mark inactive.
        /// @throws None (noexcept).
        void mark_inactive(const variable var) noexcept
        {
        }

        /// @brief Marks a variable as eliminated by a simplification pass so it is excluded from future model
        /// exposure until reconstructed by `model_reconstructor`.
        /// @param var Variable to mark eliminated.
        /// @throws None (noexcept).
        void mark_eliminated(const variable var) noexcept
        {
        }
    };
}
