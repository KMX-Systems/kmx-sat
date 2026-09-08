/// @file library/src/kmx/sat/simplify/eliminator/variable/fast.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/eliminator/variable/fast.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/eliminator/variable/fast.hpp>

namespace kmx::sat::simplify::eliminator::variable
{
    void fast::run_fast_round() noexcept
    {
        ++fast_round_count_;
        if (cheap_can_eliminate(kmx::sat::variable {1u}))
        {
            ++elimination_count_;
            bounded_.run();
        }
    }

    std::int64_t fast::cheap_score_variable(const kmx::sat::variable var) const noexcept
    {
        std::int64_t score {};
        for (const auto& clause: clauses_)
        {
            const auto occurrences = std::count_if(clause.begin(), clause.end(),
                                                   [var](const literal lit) noexcept { return lit.variable_of().index() == var.index(); });
            if (occurrences > 0L)
                ++score;
        }
        return score - 2L;
    }
}
