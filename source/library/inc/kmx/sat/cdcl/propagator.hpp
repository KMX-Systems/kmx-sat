/// @file inc/kmx/sat/cdcl/propagator.hpp
/// @brief BCP with two-watched literals and a blocking-literal fast path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/cdcl/trail.hpp>
#include <kmx/sat/cdcl/bank/watch_list.hpp>

namespace kmx::sat::cdcl
{
    /// @brief BCP with two-watched literals and a blocking-literal fast path.
    ///
    /// `propagator` implements Boolean Constraint Propagation using the two-watched-literals scheme (Chaff/MiniSat
    /// lineage): each clause watches exactly two of its literals in `bank::watch_list`, and only an assignment to one
    /// of those two literals ever requires re-examining the clause. `propagate` is the main unit-propagation loop
    /// driven by `search_coordinator` on every trail advance; `attach_clause`/`detach_clause` establish or remove a
    /// clause's pair of watched literals (invoked when a clause is created, learned, or marked garbage);
    /// `watch_clause` registers one watch entry. `propagate_assumptions` runs propagation specifically for
    /// assumption-forced literals staged by `store::assumption`, and `propagate_beyond_conflict` continues
    /// propagating implied literals needed by proof/analysis bookkeeping after a conflicting clause has already been
    /// found.
    /// @note Per the hot-path micro-optimization directives, this is one of exactly three components (with
    /// `conflict_analyzer` and `bank::watch_list`) permitted explicit software prefetch and `[[likely]]`/`[[unlikely]]`
    /// branch annotation on measured hot loops (next watch entry/clause literals), and a candidate for optional
    /// explicit SIMD watch-list scanning with scalar fallback.
    /// @reference Two-watched-literal unit propagation (Moskewicz et al., "Chaff: Engineering an Efficient SAT
    /// Solver"), with the blocking-literal fast path popularized by MiniSat/CaDiCaL.
    class propagator final
    {
    public:
        /// @brief Constructs a propagator with empty watch-list state.
        /// @throws None (noexcept).
        propagator() noexcept = default;

        /// @brief Propagates all pending trail entries until fixpoint or a conflict is found.
        /// @return Reference to the conflicting clause, or an invalid reference if propagation reached fixpoint.
        /// @throws None (noexcept).
        clause::ref_t propagate() noexcept
        {
            return {};
        }

        /// @brief Propagates assumption-forced literals staged by `store::assumption` for the current episode.
        /// @return Reference to the conflicting clause, or an invalid reference if no conflict was found.
        /// @throws None (noexcept).
        clause::ref_t propagate_assumptions() noexcept
        {
            return {};
        }

        /// @brief Continues propagating implied literals needed by proof/analysis bookkeeping after a conflict.
        /// @return Reference to a further conflicting clause, or an invalid reference if none is found.
        /// @throws None (noexcept).
        clause::ref_t propagate_beyond_conflict() noexcept
        {
            return {};
        }

        /// @brief Registers a clause's currently chosen pair of watched literals in the watch lists.
        /// @param ref Reference to the clause being watched.
        /// @throws None (noexcept).
        void watch_clause(const clause::ref_t ref) noexcept
        {
        }

        /// @brief Removes a clause's watched-literal entries, typically before deletion or relocation.
        /// @param ref Reference to the clause being detached.
        /// @throws None (noexcept).
        void detach_clause(const clause::ref_t ref) noexcept
        {
        }

        /// @brief Selects and registers the initial pair of watched literals for a newly created clause.
        /// @param ref Reference to the clause being attached.
        /// @throws None (noexcept).
        void attach_clause(const clause::ref_t ref) noexcept
        {
        }
    };
}
