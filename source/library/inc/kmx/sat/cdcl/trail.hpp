/// @file inc/kmx/sat/cdcl/trail.hpp
/// @brief Exact sequence of assigned literals.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Exact sequence of assigned literals.
    /// @details
    /// `trail` stores assignment order for propagation and backtracking. `push` appends newly assigned literals,
    /// `literal_at` provides indexed access for analyzers/propagators, and `pop_to` truncates to a prior position
    /// during backjump or restart. `propagation_head` tracks how far unit propagation has consumed the trail.
    class trail final
    {
    public:
        trail() noexcept = default;

        void push(const literal lit) noexcept { literals_.push_back(lit); }

        void pop_to(const std::uint32_t position) noexcept
        {
            if (position >= literals_.size())
            {
                literals_.clear();
                propagation_head_ = 0u;
                return;
            }
            literals_.resize(position);
            if (propagation_head_ > position)
            {
                propagation_head_ = position;
            }
        }

        std::uint32_t current_head() const noexcept { return static_cast<std::uint32_t>(literals_.size()); }

        std::uint32_t propagation_head() const noexcept { return propagation_head_; }

        void advance_propagation_head() noexcept
        {
            if (propagation_head_ < literals_.size())
            {
                ++propagation_head_;
            }
        }

        literal literal_at(const std::uint32_t position) const noexcept
        {
            if (position >= literals_.size())
            {
                return {};
            }
            return literals_[position];
        }

    private:
        std::vector<literal> literals_ {};
        std::uint32_t propagation_head_ {0};
    };
}
