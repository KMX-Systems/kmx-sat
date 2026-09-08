/// @file library/src/kmx/sat/cdcl/incremental_context.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/incremental_context.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/incremental_context.hpp>

namespace kmx::sat::cdcl
{
    void incremental_context::begin_solve_epoch() noexcept
    {
        in_epoch_ = true;
        current_epoch_retained_learned_clauses_ = 0u;
        transient_state_reset_ = false;
    }

    void incremental_context::end_solve_epoch() noexcept
    {
        if (in_epoch_)
        {
            retained_learned_clauses_ += current_epoch_retained_learned_clauses_;
            last_epoch_retained_learned_clauses_ = current_epoch_retained_learned_clauses_;
            current_epoch_retained_learned_clauses_ = 0u;
        }
        in_epoch_ = false;
    }
}
