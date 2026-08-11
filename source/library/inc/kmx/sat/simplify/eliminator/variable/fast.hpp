/// @file inc/kmx/sat/simplify/eliminator/variable/fast.hpp
/// @brief Lightweight variant of BVE for fast preprocessing.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify::eliminator::variable
{
    /// @brief Lightweight variant of BVE for fast preprocessing.
    ///
    /// @details
    /// `variable::fast` wraps a `variable::bounded` instance with cheaper, less precise scoring and eligibility
    /// checks (`cheap_score_variable`/`cheap_can_eliminate`), trading elimination thoroughness for speed on early or
    /// time-constrained preprocessing rounds where a full bounded-cost analysis would be too slow;
    /// `run_fast_round` performs one such abbreviated sweep, deferring to the owned `bounded` instance for the actual
    /// elimination mechanics (resolvent construction, extension-stack recording) once a cheap check accepts a
    /// variable.
    class fast final
    {
    public:
        /// @brief Constructs a fast eliminator with an embedded bounded eliminator.
        /// @throws None (noexcept).
        fast() noexcept = default;

        /// @brief Supplies the clause set to evaluate for elimination.
        void set_clauses(const std::vector<std::vector<literal>>& clauses) noexcept
        {
            clauses_ = clauses;
            bounded_.set_clauses(clauses);
        }

        /// @brief Runs one abbreviated, cheaply-scored elimination round.
        /// @throws None (noexcept).
        void run_fast_round() noexcept
        {
            ++fast_round_count_;
            if (cheap_can_eliminate(kmx::sat::variable {1u}))
            {
                ++elimination_count_;
                bounded_.run();
            }
        }

        /// @brief Computes a cheap, approximate elimination-cost estimate for a variable.
        /// @param var Variable to score.
        /// @return Approximate elimination cost.
        /// @throws None (noexcept).
        std::int64_t cheap_score_variable(const kmx::sat::variable var) const noexcept
        {
            std::int64_t score {0};
            for (const auto& clause : clauses_)
            {
                const auto occurrences = std::count_if(
                    clause.begin(),
                    clause.end(),
                    [var](const literal lit) noexcept { return lit.variable_of().index() == var.index(); });
                if (occurrences > 0)
                {
                    ++score;
                }
            }
            return score - 2;
        }

        /// @brief Performs a cheap, approximate eligibility check for eliminating a variable.
        /// @param var Variable to check.
        /// @return True if the cheap check accepts the variable for elimination.
        /// @throws None (noexcept).
        bool cheap_can_eliminate(const kmx::sat::variable var) const noexcept
        {
            return cheap_score_variable(var) <= 0;
        }

        /// @brief Returns how many fast rounds have been executed.
        std::uint64_t fast_round_count() const noexcept
        {
            return fast_round_count_;
        }

        /// @brief Returns how many eliminations were committed by the fast pass.
        std::uint64_t elimination_count() const noexcept
        {
            return elimination_count_;
        }

    private:
        bounded bounded_ {};
        std::vector<std::vector<literal>> clauses_ {};
        std::uint64_t fast_round_count_ {0};
        std::uint64_t elimination_count_ {0};
    };
}
