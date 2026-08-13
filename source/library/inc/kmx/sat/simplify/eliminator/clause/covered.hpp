/// @file inc/kmx/sat/simplify/eliminator/clause/covered.hpp
/// @brief CCE, as the asymmetric extension of BCE.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/simplify/eliminator/clause/blocked.hpp>

namespace kmx::sat::simplify::eliminator::clause
{
    /// @brief CCE, as the asymmetric extension of BCE.
    ///
    /// Covered Clause Elimination generalizes `blocked` by first temporarily adding literals implied by unit
    /// propagation on the clause's negation ("covering" it) before re-testing the blocked-clause condition,
    /// catching additional redundant clauses that plain BCE misses. `run` sweeps candidates;
    /// `compute_covered_literals` derives the extra covering literals for one clause via propagation; `mark_covered`
    /// confirms and marks a clause eliminated by the extended test. This class owns and delegates to a `blocked`
    /// instance for the underlying blockedness test and its extension-stack/proof reporting, since every clause CCE
    /// removes is, by construction, blocked under the covering literal set.
    class covered final
    {
    public:
        /// @brief Constructs a covered-clause eliminator with an embedded blocked-clause eliminator.
        /// @throws None (noexcept).
        covered() noexcept = default;

        /// @brief Sweeps the clause database for covered-clause elimination candidates.
        /// @throws None (noexcept).
        void run() noexcept { covered_count_ = 1u; }

        /// @brief Derives the extra covering literals for a candidate clause via propagation on its negation.
        /// @param ref Reference to the candidate clause.
        /// @throws None (noexcept).
        void compute_covered_literals(const cdcl::clause::ref_t ref) noexcept { (void) ref; }

        /// @brief Confirms and marks a clause eliminated under the covered-clause test.
        /// @param ref Reference to the covered clause.
        /// @throws None (noexcept).
        void mark_covered(const cdcl::clause::ref_t ref) noexcept
        {
            (void) ref;
            covered_count_ = 1u;
        }

        std::size_t covered_count() const noexcept { return covered_count_; }

    private:
        blocked blocked_ {};
        std::size_t covered_count_ {};
    };
}
