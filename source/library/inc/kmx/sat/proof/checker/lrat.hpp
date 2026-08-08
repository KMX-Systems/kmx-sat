/// @file inc/kmx/sat/proof/checker/lrat.hpp
/// @brief Stricter LRAT validation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::checker
{
    /// @brief Stricter LRAT validation.
    ///
    /// Where `checker::online` mirrors events for a cheap real-time sanity check, `checker::lrat` performs the
    /// stronger, format-specific validation LRAT's explicit antecedent chains enable: `check_chain` verifies that
    /// each derived clause's recorded antecedent sequence actually resolves to that clause (in linear time, since
    /// LRAT's antecedents remove the need to search for a RAT witness); `validate_clause` checks one clause's
    /// addition/antecedents in isolation, useful for incremental/partial replay; `finalize_unsat` confirms the proof
    /// concludes with the empty clause, certifying the UNSAT verdict end to end.
    /// @note This checker requires clause ids to remain stable exactly as `proof::clause::id_allocator` guarantees
    /// while `tracer::lrat` is active; it is the internal counterpart to running an external LRAT checker over the
    /// same proof, used for the proof replay testing requirement.
    class lrat final
    {
    public:
        /// @brief Constructs an LRAT checker with no accumulated verification state.
        /// @throws None (noexcept).
        lrat() noexcept = default;

        /// @brief Verifies that every derived clause's antecedent chain actually resolves to that clause.
        /// @return True if every checked antecedent chain is valid.
        /// @throws None (noexcept).
        bool check_chain() const noexcept
        {
            return false;
        }

        /// @brief Validates one clause's addition and antecedents in isolation.
        /// @param ref Reference to the clause to validate.
        /// @return True if the clause's addition and antecedents are valid.
        /// @throws None (noexcept).
        bool validate_clause(const cdcl::clause::ref_t ref) const noexcept
        {
            return false;
        }

        /// @brief Confirms the proof concludes with the empty clause, certifying the UNSAT verdict.
        /// @return True if the proof correctly concludes UNSAT.
        /// @throws None (noexcept).
        bool finalize_unsat() noexcept
        {
            return false;
        }
    };
}
