/// @file library/src/kmx/sat/simplify/engine/sweep.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/engine/sweep.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/engine/sweep.hpp>

namespace kmx::sat::simplify::engine
{
    void sweep::reset() noexcept
    {
        micro_instance_built_ = false;
        backbone_count_ = 0u;
        equivalence_count_ = 0u;
        transferred_ = false;
    }
}
