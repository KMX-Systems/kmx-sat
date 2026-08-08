/// @file inc/kmx/sat/simplify/eliminator/variable/fast.hpp
/// @brief Lightweight variant of BVE for fast preprocessing.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify::eliminator::variable
{
    /// @brief Lightweight variant of BVE for fast preprocessing.
    ///
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

        /// @brief Runs one abbreviated, cheaply-scored elimination round.
        /// @throws None (noexcept).
        void run_fast_round() noexcept
        {
        }

        /// @brief Computes a cheap, approximate elimination-cost estimate for a variable.
        /// @param var Variable to score.
        /// @return Approximate elimination cost.
        /// @throws None (noexcept).
        std::int64_t cheap_score_variable(const variable var) const noexcept
        {
            return {};
        }

        /// @brief Performs a cheap, approximate eligibility check for eliminating a variable.
        /// @param var Variable to check.
        /// @return True if the cheap check accepts the variable for elimination.
        /// @throws None (noexcept).
        bool cheap_can_eliminate(const variable var) const noexcept
        {
            return false;
        }

    private:
        bounded bounded_ {};
    };
}
