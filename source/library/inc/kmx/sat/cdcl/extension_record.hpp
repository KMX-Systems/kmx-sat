/// @file inc/kmx/sat/cdcl/extension_record.hpp
/// @brief Atomic unit stored in the extension stack: variant type for BVE eliminations, BCE blockings, factoring
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <variant>
#endif
#include <kmx/sat/cdcl/clause/view.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Records a bounded-variable-elimination (BVE) reversal: the variable removed by resolution, whose
    /// value must be re-derived from its defining clauses during reconstruction.
    /// @note Declared at namespace scope rather than nested inside `extension_record`: a nested class carrying a
    /// default member initializer is not default-constructible from within the still-incomplete enclosing class, so
    /// nesting it makes the `std::variant` member below ill-formed under a conforming compiler.
    struct bve_elimination final
    {
        variable eliminated_variable {};
        /// @brief First index, in the extension stack's witness buffer, of this record's stored clauses.
        /// @details Reconstruction needs the clauses the variable occurred in, not just its identity: the
        /// variable's value is re-derived by finding a stored clause that the rest of the model leaves unsatisfied
        /// and setting the variable to satisfy its literal there. The clauses live in one flat buffer owned by
        /// `stack::extension`, separated by a zero literal, so a record stays small and cheaply copyable.
        std::uint32_t witness_begin {};
        /// @brief One past the last index of this record's stored clauses.
        std::uint32_t witness_end {};
    };

    /// @brief Records a blocked/covered-clause-elimination (BCE/CCE) reversal: the literal that made the removed
    /// clause blocked, used to pick a satisfying polarity during reconstruction.
    struct bce_blocking final
    {
        literal blocking_literal {};
        /// @brief First index, in the extension stack's witness buffer, of the removed clause.
        std::uint32_t witness_begin {};
        /// @brief One past the last index of the removed clause.
        std::uint32_t witness_end {};
    };

    /// @brief Records a factoring/BVA transformation reversal: an internally introduced variable whose value
    /// must be dropped (never exposed externally) rather than reconstructed.
    struct factor_transformation final
    {
        variable introduced_variable {};
    };

    /// @brief Atomic unit stored in the extension stack: variant type for BVE eliminations, BCE blockings, factoring
    /// transformations, and other reversible operations.
    /// @details
    /// Every simplification pass that removes information a satisfying assignment might need (a bounded-variable
    /// elimination, a blocked/covered clause removal, a factoring/BVA substitution) must record enough to reverse
    /// that removal when reconstructing the external model; `extension_record` is the closed-set (`std::variant`)
    /// payload type for exactly that, keeping one small, cheaply copyable record per reversible transformation
    /// instead of a class hierarchy. `model_reconstructor::apply_extension_record` interprets each variant
    /// alternative to restore the eliminated/blocked/introduced variable's correct value in reverse chronological
    /// order (last simplification undone first).
    struct extension_record final
    {
        /// @brief Alias keeping the payload types reachable through the record, as call sites spell them.
        using bve_elimination = kmx::sat::cdcl::bve_elimination;
        /// @brief Alias keeping the payload types reachable through the record, as call sites spell them.
        using bce_blocking = kmx::sat::cdcl::bce_blocking;
        /// @brief Alias keeping the payload types reachable through the record, as call sites spell them.
        using factor_transformation = kmx::sat::cdcl::factor_transformation;

        /// @brief The concrete reversible-transformation payload for this record.
        std::variant<bve_elimination, bce_blocking, factor_transformation> payload {};
    };
}
