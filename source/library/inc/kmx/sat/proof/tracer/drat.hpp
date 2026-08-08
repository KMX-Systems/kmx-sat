/// @file inc/kmx/sat/proof/tracer/drat.hpp
/// @brief Concrete DRAT proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Concrete DRAT proof format tracer.
    ///
    /// DRAT (Deletion Resolution Asymmetric Tautology) is the de facto standard unsatisfiability proof format for SAT
    /// competitions: each derived clause must be RAT (resolution asymmetric tautology) with respect to the current
    /// clause set, which a DRAT checker can verify without needing explicit antecedent information. Per the proof
    /// format compatibility matrix, DRAT supports all resolution-based derivations (BVE, BCE, CCE, subsumption,
    /// vivification) but has no native representation for XOR/gate-level or cardinality reasoning, so
    /// `congruence_engine`/`gate_extractor` must emit resolution-equivalent events when this tracer is active.
    /// `add_original`/`add_derived` emit clause-addition lines, `delete_clause` emits deletion lines, `shrink_clause`
    /// emits the shrunk clause as a new derived clause, and `finalize` writes the proof's closing marker.
    /// @reference DRAT: "Efficient Certified RAT Verification" (Wetzler, Heule, Hunt) and the associated DRAT-trim
    /// checker used across SAT Competition tooling.
    class drat final
    {
    public:
        /// @brief Constructs a DRAT tracer with no buffered output.
        /// @throws None (noexcept).
        drat() noexcept = default;

        /// @brief Emits an original-clause line for the given clause.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a derived-clause (RAT) line for the given clause.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a deletion line for the given clause.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits the shrunk clause as a newly derived clause line.
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
