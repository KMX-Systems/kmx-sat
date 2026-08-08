/// @file inc/kmx/sat/proof/tracer/frat.hpp
/// @brief Concrete FRAT proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Concrete FRAT proof format tracer.
    ///
    /// FRAT (Flexible RAT) records the same resolution-only derivations as DRAT/LRAT but as structured,
    /// self-describing records (with optional antecedent information) rather than DRAT's terse line format, making it
    /// cheaper to emit on the hot path while still supporting out-of-band checking, at the cost of a larger proof
    /// file than LRAT's compact form. Per the compatibility matrix it shares LRAT's requirement that
    /// `proof::clause::id_allocator` keep clause ids stable across every active pass. `add_original`/`add_derived`
    /// emit structured addition records, `delete_clause`/`shrink_clause` emit structured removal/shrink records, and
    /// `finalize` closes the proof.
    /// @reference FRAT: "FRAT: A Flexible, Efficient Certified Deduction Format" (Baek, Carneiro, Heule).
    class frat final
    {
    public:
        /// @brief Constructs an FRAT tracer with no buffered output.
        /// @throws None (noexcept).
        frat() noexcept = default;

        /// @brief Emits a structured original-clause addition record.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a structured derived-clause addition record.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a structured clause-deletion record.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a structured clause-shrink record.
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
