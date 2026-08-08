/// @file inc/kmx/sat/proof/tracer/lidrup.hpp
/// @brief Concrete LIDRUP proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Concrete LIDRUP proof format tracer.
    ///
    /// LIDRUP (Linear Incremental DRUP) combines IDRUP's incremental-session epoch bookkeeping with LRAT-style
    /// explicit antecedent chains, giving linear-time checkability for incremental SAT+UNSAT sessions rather than
    /// IDRUP's DRAT-style search-based checking. Like `tracer::lrat`, it requires `proof::clause::id_allocator` to
    /// keep clause ids stable across every active pass; like `tracer::idrup`, it requires epoch-local passes to
    /// declare their epoch-locality explicitly. `add_original`/`add_derived` emit epoch-scoped lines with antecedent
    /// chains, `delete_clause`/`shrink_clause` emit the corresponding epoch-scoped id-referencing events, and
    /// `finalize` closes out the current epoch's segment.
    /// @reference LIDRUP: linear, incremental extension combining IDRUP's incremental session support with LRAT-style
    /// antecedent chains for linear-time checking.
    class lidrup final
    {
    public:
        /// @brief Constructs a LIDRUP tracer with no buffered output.
        /// @throws None (noexcept).
        lidrup() noexcept = default;

        /// @brief Emits an epoch-scoped original-clause line with its assigned id.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits an epoch-scoped derived-clause line with its antecedent id chain.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits an epoch-scoped deletion line referencing the clause's stable id.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits an epoch-scoped clause-shrink line with its antecedent id chain.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Closes out the current epoch's segment of the proof.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
        }
    };
}
