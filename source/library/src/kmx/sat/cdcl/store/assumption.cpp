/// @file library/src/kmx/sat/cdcl/store/assumption.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/store/assumption.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/store/assumption.hpp>

namespace kmx::sat::cdcl::store
{
    void assumption::clear() noexcept
    {
        literals_.clear();
        failed_assumptions_.clear();
        trail_level_base_ = 0u;
    }
}
