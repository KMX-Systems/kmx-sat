/// @file library/src/kmx/sat/cdcl/stack/decision_frame.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/stack/decision_frame.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/stack/decision_frame.hpp>

namespace kmx::sat::cdcl::stack
{
    void decision_frame::pop_to_level(const std::uint32_t level) noexcept
    {
        if (level >= frames_.size())
        {
            frames_.clear();
            current_trail_base_ = 0u;
            return;
        }
        frames_.resize(level);
    }

    literal decision_frame::decision_literal(const std::uint32_t level) const noexcept
    {
        if ((level == 0u) || (level > frames_.size()))
            return {};
        return frames_[level - 1u].decision;
    }

    std::uint32_t decision_frame::trail_base_of_level(const std::uint32_t level) const noexcept
    {
        if ((level == 0u) || (level > frames_.size()))
            return 0u;
        return frames_[level - 1u].trail_base;
    }
}
