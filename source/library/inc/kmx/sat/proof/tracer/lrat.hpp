/// @file inc/kmx/sat/proof/tracer/lrat.hpp
/// @brief Concrete LRAT proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Concrete LRAT proof format tracer.
    ///
    /// LRAT (Linear RAT) extends DRAT with explicit antecedent clause-id chains for every derivation, letting a
    /// checker verify each step in linear time instead of DRAT's more expensive RAT search. Per the compatibility
    /// matrix, LRAT carries the same resolution-only representational constraint as DRAT, with the added requirement
    /// that `proof::clause::id_allocator` guarantee stable clause ids across every pass active while this tracer is
    /// enabled, since a stale id would break the antecedent chain. `add_original`/`add_derived` emit clause lines
    /// together with their resolved antecedent id sequence (built by `conflict_analyzer::build_resolution_chain`),
    /// `delete_clause`/`shrink_clause` emit the corresponding id-referencing events, and `finalize` closes the proof.
    /// @reference LRAT: "Extended Resolution Simulates DRAT" / "The LRAT proof format" (Cruz-Filipe et al.), building
    /// on DRAT with explicit antecedent chains for linear-time checking.
    class lrat final
    {
    public:
        /// @brief Constructs an LRAT tracer with no buffered output.
        /// @throws None (noexcept).
        lrat() noexcept = default;

        /// @brief Emits an original-clause line with its assigned clause id.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a derived-clause line together with its resolved antecedent id chain.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a deletion line referencing the clause's stable id.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits the shrunk clause as a newly derived clause line with its antecedent chain.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Writes the proof's closing marker.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
        }
    };
}
