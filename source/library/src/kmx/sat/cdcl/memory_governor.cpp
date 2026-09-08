/// @file library/src/kmx/sat/cdcl/memory_governor.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/memory_governor.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/memory_governor.hpp>

namespace kmx::sat::cdcl
{
    void memory_governor::register_budget(const std::size_t soft_ceiling_bytes, const std::size_t hard_ceiling_bytes) noexcept
    {
        soft_ceiling_bytes_ = soft_ceiling_bytes;
        hard_ceiling_bytes_ = hard_ceiling_bytes;
        current_usage_ = 0u;
        soft_limit_breached_ = false;
        hard_limit_breached_ = false;
        shrink_requests_ = 0u;
        escalation_steps_ = 0u;
    }

    void memory_governor::reset_epoch_usage() noexcept
    {
        current_usage_ = 0u;
        soft_limit_breached_ = false;
        hard_limit_breached_ = false;
    }
}
