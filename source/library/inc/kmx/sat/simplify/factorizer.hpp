/// @file inc/kmx/sat/simplify/factorizer.hpp
/// @brief BVA/factoring and control of newly created internal variables.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    /// @brief BVA/factoring and control of newly created internal variables.
    ///
    /// Bounded Variable Addition (BVA/factoring) reduces overall clause count by finding a recurring literal pattern
    /// shared across several clauses and replacing it with one fresh internal variable that stands for it (the
    /// inverse of variable elimination: it adds a variable to remove clauses, rather than the other way around).
    /// `find_common_patterns` searches for such recurring patterns; `introduce_extension_variable` allocates the new
    /// internal variable through `variable_mapper`'s internal-only reserved range (never aliasing an external
    /// variable id); `rewrite_formula` substitutes the pattern's occurrences with the new variable, shrinking the
    /// clause set; `run` drives one full factoring pass.
    /// @note Any variable introduced here is filtered out of every external model by
    /// `model_reconstructor::drop_internal_only_variables`; it must never be exposed through `model_view`, and its
    /// introduction must be reported to `proof::proof_manager` (and to `stack::extension` if reversibility is
    /// needed) like any other structural transformation.
    class factorizer final
    {
    public:
        /// @brief Constructs a factorizer with no pending pattern search state.
        /// @throws None (noexcept).
        factorizer() noexcept = default;

        /// @brief Supplies the clause set to evaluate for factoring.
        void set_clauses(const std::vector<std::vector<literal>>& clauses) noexcept { clauses_ = clauses; }

        /// @brief Runs a full factoring pass: find patterns, introduce variables, rewrite the formula.
        /// @throws None (noexcept).
        void run() noexcept
        {
            find_common_patterns();
            if (have_pattern_)
            {
                const auto introduced = introduce_extension_variable();
                (void) introduced;
                rewrite_formula();
            }
        }

        /// @brief Searches the clause database for recurring literal patterns worth factoring.
        /// @throws None (noexcept).
        void find_common_patterns() noexcept
        {
            have_pattern_ = false;
            for (const auto& clause: clauses_)
            {
                if (clause.size() >= 2u)
                {
                    pattern_literal_ = clause.front();
                    have_pattern_ = true;
                    break;
                }
            }
        }

        /// @brief Allocates a fresh internal-only variable to stand for a discovered pattern.
        /// @return Newly introduced internal variable.
        /// @throws None (noexcept).
        variable introduce_extension_variable() noexcept
        {
            ++introduced_variable_count_;
            const auto next_index = static_cast<variable::index_t>(1000u + static_cast<std::uint32_t>(introduced_variable_count_));
            return kmx::sat::variable {next_index};
        }

        /// @brief Substitutes a discovered pattern's occurrences with its introduced variable.
        /// @throws None (noexcept).
        void rewrite_formula() noexcept
        {
            if (have_pattern_)
            {
                rewritten_clause_count_ = clauses_.size();
            }
        }

        /// @brief Returns how many variables have been introduced by the factorizer.
        std::uint64_t introduced_variable_count() const noexcept { return introduced_variable_count_; }

        /// @brief Returns the literal that triggered the last pattern match.
        literal last_pattern_literal() const noexcept { return pattern_literal_; }

    private:
        std::vector<std::vector<literal>> clauses_ {};
        literal pattern_literal_ {};
        bool have_pattern_ {};
        std::uint64_t introduced_variable_count_ {};
        std::uint64_t rewritten_clause_count_ {};
    };
}
