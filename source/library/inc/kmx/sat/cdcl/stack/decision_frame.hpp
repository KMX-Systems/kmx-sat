/// @file inc/kmx/sat/cdcl/stack/decision_frame.hpp
/// @brief Control frames in the CaDiCaL/Kissat style for decisions and backtracking.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/cdcl/trail.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::stack
{
    /// @brief One decision level record: decision literal and trail base.
    struct frame
    {
        literal decision {};
        std::uint32_t trail_base {};
    };

    /// @brief Control frames in the CaDiCaL/Kissat style for decisions and backtracking.
    /// @details
    /// `decision_frame` tracks per-level decision literals and corresponding trail bases so conflict analysis and
    /// backtracking can recover the boundary of each decision level efficiently.
    class decision_frame final
    {
    public:
        decision_frame() noexcept = default;

        void push_frame(const literal decision) noexcept
        {
            frames_.push_back(frame {decision, current_trail_base_});
            current_trail_base_ = 0u;
        }

        void pop_to_level(const std::uint32_t level) noexcept;

        std::uint32_t current_level() const noexcept { return static_cast<std::uint32_t>(frames_.size()); }

        literal decision_literal(const std::uint32_t level) const noexcept;

        std::uint32_t trail_base_of_level(const std::uint32_t level) const noexcept;

        void reuse_trail_metadata() noexcept
        {
            if (!frames_.empty())
                frames_.back().trail_base = current_trail_base_;
        }

        void set_current_trail_base(const std::uint32_t base) noexcept { current_trail_base_ = base; }

    private:
        std::vector<frame> frames_ {};
        std::uint32_t current_trail_base_ {};
    };
}
