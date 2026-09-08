/// @file library/src/kmx/sat/simplify/eliminator/clause/blocked.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/eliminator/clause/blocked.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/eliminator/clause/blocked.hpp>

namespace kmx::sat::simplify::eliminator::clause
{
    bool blocked::is_blocked_on(const cdcl::clause::ref_t ref, const literal lit) const noexcept
    {
        (void)ref;
        (void)lit;
        return true;
    }
}
