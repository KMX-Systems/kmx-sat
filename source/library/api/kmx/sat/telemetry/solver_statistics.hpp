/// @file api/kmx/sat/telemetry/solver_statistics.hpp
/// @brief All operational counters and metrics collected during search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <string>
    #include <string_view>
    #include <unordered_map>
#endif

namespace kmx::sat::telemetry
{
    /// @brief All operational counters and metrics collected during search.
    ///
    /// @details
    /// `solver_statistics` is the single accumulator every hot-path subsystem reports through: `inc`/`add` update a
    /// named counter (conflicts, decisions, propagations, restarts, learned clauses, and any other metric a
    /// subsystem chooses to name), `snapshot_of` captures an immutable `snapshot` for exposure through
    /// `solve_result::statistics_snapshot`/`solver::statistics`, `merge_phase_statistics` combines counters from a
    /// sub-phase (for example a portfolio worker or a parallel preprocessing sub-task) into a parent accumulator, and
    /// `reset_epoch_counters` clears per-epoch counters at incremental session boundaries without necessarily
    /// resetting lifetime totals. The `snapshot` type is the stable, minimal, copyable view exposed across the public
    /// API boundary.
    /// @note This class is one of the minimum public interfaces required to stabilize early and follows semantic
    /// versioning; `report_formatter`/`ema_tracker` are the primary internal consumers of its data.
    class solver_statistics final
    {
    public:
        /// @brief Immutable point-in-time copy of the tracked counters.
        struct snapshot final
        {
            /// @brief Total number of conflicts encountered so far.
            std::uint64_t conflicts {};
            /// @brief Total number of branching decisions made so far.
            std::uint64_t decisions {};
            /// @brief Total number of unit propagations performed so far.
            std::uint64_t propagations {};
            /// @brief Total number of restarts performed so far.
            std::uint64_t restarts {};
            /// @brief Total number of clauses learned from conflict analysis so far.
            std::uint64_t learned_clauses {};
        };

        /// @brief Constructs a statistics accumulator with every counter at zero.
        /// @throws None (noexcept).
        solver_statistics() noexcept = default;

        /// @brief Increments a named counter by one.
        /// @param counter_name Identifier of the counter to increment.
        /// @throws None (noexcept).
        void inc(const std::string_view counter_name) noexcept
        {
            add(counter_name, 1u);
        }

        /// @brief Adds a given amount to a named counter.
        /// @param counter_name Identifier of the counter to update.
        /// @param amount Amount to add.
        /// @throws None (noexcept).
        void add(const std::string_view counter_name, const std::uint64_t amount) noexcept
        {
            snapshot_.conflicts += counter_name == "conflicts" ? amount : 0u;
            snapshot_.decisions += counter_name == "decisions" ? amount : 0u;
            snapshot_.propagations += counter_name == "propagations" ? amount : 0u;
            snapshot_.restarts += counter_name == "restarts" ? amount : 0u;
            snapshot_.learned_clauses += counter_name == "learned_clauses" ? amount : 0u;
        }

        /// @brief Captures an immutable snapshot of the currently tracked counters.
        /// @return Point-in-time copy of every tracked counter.
        /// @throws None (noexcept).
        snapshot snapshot_of() const noexcept
        {
            return snapshot_;
        }

        /// @brief Merges counters accumulated by a sub-phase (portfolio worker, parallel sub-task) into this instance.
        /// @param other Statistics accumulator whose counters should be merged in.
        /// @throws None (noexcept).
        void merge_phase_statistics(const solver_statistics& other) noexcept
        {
            snapshot_.conflicts += other.snapshot_.conflicts;
            snapshot_.decisions += other.snapshot_.decisions;
            snapshot_.propagations += other.snapshot_.propagations;
            snapshot_.restarts += other.snapshot_.restarts;
            snapshot_.learned_clauses += other.snapshot_.learned_clauses;
        }

        /// @brief Resets per-epoch counters at an incremental session boundary.
        /// @throws None (noexcept).
        void reset_epoch_counters() noexcept
        {
            snapshot_.conflicts = 0u;
            snapshot_.decisions = 0u;
            snapshot_.propagations = 0u;
            snapshot_.restarts = 0u;
            snapshot_.learned_clauses = 0u;
        }

    private:
        snapshot snapshot_ {};
    };
}
