/// @file inc/kmx/sat/cdcl/memory_governor.hpp
/// @brief Single authority for tracking and enforcing memory ceilings across all clause, index, and
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif

#include <limits>

namespace kmx::sat::cdcl
{
    /// @brief Single authority for tracking and enforcing memory ceilings across all clause, index, and
    /// proof-buffering subsystems, decoupled from the mechanics of any one allocator.
    /// @details
    /// Per the resource-limits policy, ceilings are optional and, when set through `register_budget`, apply to a
    /// total process-relevant budget spanning clause arenas (`bank::arena`), watch lists (`bank::watch_list`),
    /// transient preprocessing indexes, proof buffering (`proof::event_stream`/`io::proof_output_pipeline`), and
    /// extension/model-reconstruction metadata, even though this class tracks per-category usage internally for
    /// diagnostics. `soft_limit_breached`/`hard_limit_breached` are polled by `reduce_controller`,
    /// `flush_restore_manager`, `simplify::scheduler::inprocess`, and `simplify::scheduler::preprocess` to drive the
    /// two-stage escalation ladder: a soft breach triggers out-of-band reduction, more aggressive flushing, and
    /// disabling memory-heavy passes for the rest of the epoch; a hard breach pauses all learned-clause growth,
    /// suspends every simplification scheduler for the remainder of the `solve()` call, and — if shedding cannot
    /// bring usage back under the ceiling — ends the call with `solve_result::status::terminated` rather than
    /// crashing. `reset_epoch_usage` is called at each `incremental_context::begin_solve_epoch` so a prior epoch's
    /// transient overshoot never permanently biases later escalation decisions.
    /// @note If no ceilings are registered, the solver runs unbounded and only OS-level allocation failure applies;
    /// embedding frontends (competition CLI, portfolio launcher) are expected to set both ceilings explicitly.
    class memory_governor final
    {
    public:
        /// @brief Constructs a memory governor with no registered budget.
        /// @throws None (noexcept).
        memory_governor() noexcept = default;

        /// @brief Registers the soft and hard memory ceilings tracked by this governor.
        /// @param soft_ceiling_bytes Usage threshold that triggers the soft-breach escalation ladder.
        /// @param hard_ceiling_bytes Usage threshold that triggers the hard-breach escalation ladder.
        /// @throws None (noexcept).
        void register_budget(const std::size_t soft_ceiling_bytes, const std::size_t hard_ceiling_bytes) noexcept;

        /// @brief Returns the current tracked memory usage across every registered category.
        /// @return Current usage in bytes.
        /// @throws None (noexcept).
        std::size_t current_usage() const noexcept { return current_usage_; }

        /// @brief Sets the currently tracked aggregate memory usage.
        /// @param usage_bytes Aggregate usage in bytes across tracked subsystems.
        /// @throws None (noexcept).
        void set_current_usage(const std::size_t usage_bytes) noexcept
        {
            current_usage_ = usage_bytes;
            refresh_breach_state();
        }

        /// @brief Checks whether current usage has crossed the registered soft ceiling.
        /// @return True if the soft ceiling is currently breached.
        /// @throws None (noexcept).
        bool soft_limit_breached() const noexcept { return soft_limit_breached_; }

        /// @brief Checks whether current usage has crossed the registered hard ceiling.
        /// @return True if the hard ceiling is currently breached.
        /// @throws None (noexcept).
        bool hard_limit_breached() const noexcept { return hard_limit_breached_; }

        /// @brief Requests that dependent subsystems shed memory (reduce, flush, disable heavy passes).
        /// @throws None (noexcept).
        void request_shrink() noexcept
        {
            ++shrink_requests_;
            refresh_breach_state();
        }

        /// @brief Advances to the next stage of the soft/hard escalation ladder.
        /// @throws None (noexcept).
        void escalate_policy() noexcept
        {
            ++escalation_steps_;
            refresh_breach_state();
        }

        /// @brief Resets per-epoch usage accounting at the start of a new solve epoch.
        /// @throws None (noexcept).
        void reset_epoch_usage() noexcept;

        /// @brief Returns how many shrink requests were made.
        /// @return Number of shrink requests.
        std::size_t shrink_requests() const noexcept { return shrink_requests_; }

        /// @brief Returns how many escalation steps were recorded.
        /// @return Number of escalation steps.
        std::size_t escalation_steps() const noexcept { return escalation_steps_; }

    private:
        void refresh_breach_state() noexcept
        {
            soft_limit_breached_ = current_usage_ >= soft_ceiling_bytes_;
            hard_limit_breached_ = current_usage_ >= hard_ceiling_bytes_;
        }

        std::size_t current_usage_ {};
        std::size_t soft_ceiling_bytes_ = std::numeric_limits<std::size_t>::max();
        std::size_t hard_ceiling_bytes_ = std::numeric_limits<std::size_t>::max();
        bool soft_limit_breached_ {};
        bool hard_limit_breached_ {};
        std::size_t shrink_requests_ {};
        std::size_t escalation_steps_ {};
    };
}
