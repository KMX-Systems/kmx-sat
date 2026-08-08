/// @file inc/kmx/sat/cdcl/trail.hpp
/// @brief Exact sequence of assigned literals.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Exact sequence of assigned literals.
    ///
    /// `trail` records, in strict chronological order, every literal assigned so far (decisions and their
    /// propagated consequences interleaved), which is what makes it possible to unwind state deterministically on
    /// backtrack. `propagation_head`/`advance_propagation_head` track the classic two-pointer propagation scheme:
    /// the propagation head lags behind `current_head` while `propagator::propagate` still has unprocessed trail
    /// entries to examine, and catches up as each is processed; `pop_to` truncates the trail (and, through
    /// `store::assignment`, clears the corresponding values/reasons/levels) back to a given position during
    /// backtracking.
    /// @note This class only tracks positions and literal identities; it delegates value/level/reason storage to
    /// `store::assignment`, and decision-level bookkeeping to `stack::decision_frame`.
    class trail final
    {
    public:
        /// @brief Constructs an empty trail.
        /// @throws None (noexcept).
        trail() noexcept = default;

        /// @brief Appends one newly assigned literal to the end of the trail.
        /// @param lit Literal that was just assigned.
        /// @throws None (noexcept).
        void push(const literal lit) noexcept
        {
        }

        /// @brief Truncates the trail back to a given position, discarding everything assigned after it.
        /// @param position Trail position to truncate back to.
        /// @throws None (noexcept).
        void pop_to(const std::uint32_t position) noexcept
        {
        }

        /// @brief Returns the current length of the trail (the next free position).
        /// @return Current trail head position.
        /// @throws None (noexcept).
        std::uint32_t current_head() const noexcept
        {
            return {};
        }

        /// @brief Returns the position of the next trail entry awaiting propagation.
        /// @return Propagation head position.
        /// @throws None (noexcept).
        std::uint32_t propagation_head() const noexcept
        {
            return {};
        }

        /// @brief Advances the propagation head past the entry that was just processed.
        /// @throws None (noexcept).
        void advance_propagation_head() noexcept
        {
        }

        /// @brief Returns the literal assigned at a given trail position.
        /// @param position Trail position to query.
        /// @return Literal assigned at that position.
        /// @throws None (noexcept).
        literal literal_at(const std::uint32_t position) const noexcept
        {
            return {};
        }
    };
}
