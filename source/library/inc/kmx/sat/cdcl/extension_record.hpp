/// @file inc/kmx/sat/cdcl/extension_record.hpp
/// @brief Atomic unit stored in the extension stack: variant type for BVE eliminations, BCE blockings, factoring
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <variant>
#endif
#include <kmx/sat/cdcl/clause/view.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Atomic unit stored in the extension stack: variant type for BVE eliminations, BCE blockings, factoring
    /// transformations, and other reversible operations.
    ///
    /// Every simplification pass that removes information a satisfying assignment might need (a bounded-variable
    /// elimination, a blocked/covered clause removal, a factoring/BVA substitution) must record enough to reverse
    /// that removal when reconstructing the external model; `extension_record` is the closed-set (`std::variant`)
    /// payload type for exactly that, keeping one small, cheaply copyable record per reversible transformation
    /// instead of a class hierarchy. `model_reconstructor::apply_extension_record` interprets each variant
    /// alternative to restore the eliminated/blocked/introduced variable's correct value in reverse chronological
    /// order (last simplification undone first).
    struct extension_record final
    {
        /// @brief Records a bounded-variable-elimination (BVE) reversal: the variable removed by resolution, whose
        /// value must be re-derived from its defining clauses during reconstruction.
        struct bve_elimination final
        {
            variable eliminated_variable {};
        };

        /// @brief Records a blocked/covered-clause-elimination (BCE/CCE) reversal: the literal that made the removed
        /// clause blocked, used to pick a satisfying polarity during reconstruction.
        struct bce_blocking final
        {
            literal blocking_literal {};
        };

        /// @brief Records a factoring/BVA transformation reversal: an internally introduced variable whose value
        /// must be dropped (never exposed externally) rather than reconstructed.
        struct factor_transformation final
        {
            variable introduced_variable {};
        };

        /// @brief The concrete reversible-transformation payload for this record.
        std::variant<bve_elimination, bce_blocking, factor_transformation> payload {};
    };
}
