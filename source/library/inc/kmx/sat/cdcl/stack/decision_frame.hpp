/// @file inc/kmx/sat/cdcl/stack/decision_frame.hpp
/// @brief Control frames in the CaDiCaL/Kissat style for decisions and backtracking.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/cdcl/trail.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::stack
{
    /// @brief Control frames in the CaDiCaL/Kissat style for decisions and backtracking.
    ///
    /// Each decision made by `engine::decision` opens one frame recording the decision literal and the trail
    /// position at which it begins; `stack::decision_frame` is the stack of those frames. `current_level` is simply
    /// the number of open frames; `pop_to_level` discards frames above a target level during `backtrack_engine`
    /// unwinding (whether after a conflict or a restart); `trail_base_of_level` gives `conflict_analyzer` the trail
    /// range owned by a given decision level, used when scanning backward for the first-UIP cut.
    /// `reuse_trail_metadata` supports partial/chronological backtracking styles where some frame bookkeeping is kept
    /// intact across a backjump instead of being fully rebuilt, and can be informed by
    /// `assumption_reuse_advisor::suggest_trail_reuse_depth` in assumption-heavy incremental workloads.
    class decision_frame final
    {
    public:
        /// @brief Constructs an empty decision-frame stack (decision level zero).
        /// @throws None (noexcept).
        decision_frame() noexcept = default;

        /// @brief Opens a new decision-level frame for the given decision literal.
        /// @param decision Literal chosen as the new decision.
        /// @throws None (noexcept).
        void push_frame(const literal decision) noexcept
        {
        }

        /// @brief Discards every frame above the given decision level.
        /// @param level Target decision level to pop back to.
        /// @throws None (noexcept).
        void pop_to_level(const std::uint32_t level) noexcept
        {
        }

        /// @brief Returns the current decision level (number of open frames).
        /// @return Current decision level.
        /// @throws None (noexcept).
        std::uint32_t current_level() const noexcept
        {
            return {};
        }

        /// @brief Returns the decision literal that opened a given decision level.
        /// @param level Decision level to query.
        /// @return Decision literal for that level.
        /// @throws None (noexcept).
        literal decision_literal(const std::uint32_t level) const noexcept
        {
            return {};
        }

        /// @brief Returns the trail position at which a given decision level begins.
        /// @param level Decision level to query.
        /// @return Trail base position for that level.
        /// @throws None (noexcept).
        std::uint32_t trail_base_of_level(const std::uint32_t level) const noexcept
        {
            return {};
        }

        /// @brief Preserves frame bookkeeping across a backjump for partial/chronological backtracking reuse.
        /// @throws None (noexcept).
        void reuse_trail_metadata() noexcept
        {
        }
    };
}
