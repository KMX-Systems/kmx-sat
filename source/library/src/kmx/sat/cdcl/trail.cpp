/// @file library/src/kmx/sat/cdcl/trail.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/trail.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/trail.hpp>

namespace kmx::sat::cdcl
{
    void trail::pop_to(const std::uint32_t position) noexcept
    {
        if (position >= literals_.size())
        {
            literals_.clear();
            propagation_head_ = 0u;
            return;
        }
        literals_.resize(position);
        if (propagation_head_ > position)
            propagation_head_ = position;
    }

    literal trail::literal_at(const std::uint32_t position) const noexcept
    {
        if (position >= literals_.size())
            return {};
        return literals_[position];
    }
}
