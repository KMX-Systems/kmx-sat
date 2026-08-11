/// @file inc/kmx/sat/cdcl/assumption_reuse_advisor.hpp
/// @brief Learns lightweight ordering and trail-reuse hints from prior solve epochs to reduce repeated
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::cdcl
{
    /// @brief Learns lightweight ordering and trail-reuse hints from prior solve epochs to reduce repeated
    /// propagation/restart work on assumption-heavy incremental workloads. Research-track optimization.
    ///
    /// Repeated-solving use cases (bounded model checking, ILP-style repeated calls) tend to re-issue similar
    /// assumption sets across many `solver::solve` episodes; naive handling re-propagates and potentially re-decides
    /// the same prefix every time. `record_epoch_outcome` observes, after each episode, which assumption prefix
    /// stayed consistent with the trail and how much of the trail could have been reused; `suggest_assumption_order`
    /// proposes an ordering for the next episode's assumptions that maximizes prefix reuse against
    /// `decision_frame_stack::reuse_trail_metadata`; `suggest_trail_reuse_depth` gives `backtrack_engine` an upper
    /// bound on how far the trail can be kept instead of unwound. `reset_history` discards learned hints, for example
    /// after `solver::reset_session`.
    /// @note This component is explicitly research-track: it must be validated on the comparative benchmarking
    /// harness against pinned CaDiCaL/Kissat releases before any of its suggestions are promoted to an always-on
    /// default; it never changes solve correctness, only scheduling/ordering heuristics.
    class assumption_reuse_advisor final
    {
    public:
        /// @brief Constructs an advisor with no learned history.
        /// @throws None (noexcept).
        assumption_reuse_advisor() noexcept = default;

        /// @brief Records the outcome of the just-finished solve epoch to refine future suggestions.
        /// @throws None (noexcept).
        void record_epoch_outcome() noexcept
        {
            if (trail_reuse_depth_ < max_depth_)
            {
                ++trail_reuse_depth_;
            }
        }

        /// @brief Proposes an assumption ordering for the next episode that favors trail-prefix reuse.
        /// @throws None (noexcept).
        void suggest_assumption_order() noexcept
        {
            has_suggested_order_ = true;
        }

        /// @brief Suggests how many trail levels may safely be reused rather than unwound for the next episode.
        /// @return Suggested trail-reuse depth, in decision levels.
        /// @throws None (noexcept).
        std::uint32_t suggest_trail_reuse_depth() const noexcept
        {
            return trail_reuse_depth_;
        }

        /// @brief Returns whether the advisor has already suggested an assumption ordering for the current session.
        /// @return True when a suggestion has been issued.
        /// @throws None (noexcept).
        bool has_suggested_order() const noexcept
        {
            return has_suggested_order_;
        }

        /// @brief Discards all learned ordering/reuse history, for example after a full session reset.
        /// @throws None (noexcept).
        void reset_history() noexcept
        {
            trail_reuse_depth_ = 0u;
            has_suggested_order_ = false;
        }
    private:
        std::uint32_t trail_reuse_depth_ {0u};
        std::uint32_t max_depth_ {4u};
        bool has_suggested_order_ {false};
    };
}
