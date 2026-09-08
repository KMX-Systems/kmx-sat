/// @file api/kmx/sat/telemetry/solver_statistics.hpp
/// @brief All operational counters and metrics collected during search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <string_view>
#endif
#include <kmx/sat/counter.hpp>

namespace kmx::sat::telemetry
{
    /// @brief Identifies one counter tracked by `solver_statistics`.
    /// @details Every enumerator names exactly one field of `solver_statistics::snapshot`, so a reporting subsystem
    /// selects a counter by identity rather than by a spelled-out name that no compiler can check.
    enum class counter_id : std::uint8_t
    {
        conflicts,
        decisions,
        propagations,
        restarts,
        learned_clauses,
        learned_clause_glue_total,
        learned_clause_glue_samples,
        reduction_passes,
        reduced_clauses,
        deleted_clauses,
        probes,
        probe_units,
        walk_flips,
        terminate_callback_calls,
        learn_callback_calls,
        external_propagator_calls,
        option_updates,
        configuration_updates,
    };

    /// @brief Number of counters in `counter_id`, and the size of any per-counter table.
    inline constexpr std::size_t counter_count {static_cast<std::size_t>(counter_id::configuration_updates) + 1u};

    /// @brief Field name each counter is reported under, indexed by `counter_id`.
    /// @details This is the only place a counter is spelled out: `report_formatter` emits these names and the
    /// report parsers resolve them back to a `counter_id`, so the emitted and expected vocabularies cannot drift.
    inline constexpr std::array<std::string_view, counter_count> counter_names {"conflicts",
                                                                                "decisions",
                                                                                "propagations",
                                                                                "restarts",
                                                                                "learned_clauses",
                                                                                "learned_clause_glue_total",
                                                                                "learned_clause_glue_samples",
                                                                                "reduction_passes",
                                                                                "reduced_clauses",
                                                                                "deleted_clauses",
                                                                                "probes",
                                                                                "probe_units",
                                                                                "walk_flips",
                                                                                "terminate_callback_calls",
                                                                                "learn_callback_calls",
                                                                                "external_propagator_calls",
                                                                                "option_updates",
                                                                                "configuration_updates"};

    /// @brief Returns the reporting field name of one counter.
    /// @param id Counter to name.
    /// @return Field name used in statistics report lines.
    /// @throws None (noexcept).
    constexpr std::string_view name_of(const counter_id id) noexcept
    {
        return counter_names[static_cast<std::uint8_t>(id)];
    }

    /// @brief Maps a field name read back from a statistics report onto its counter.
    /// @param name Field name to resolve.
    /// @return Resolved counter, or an empty optional when the field is not a counter.
    /// @throws None (noexcept).
    constexpr std::optional<counter_id> parse_counter_id(const std::string_view name) noexcept
    {
        for (std::size_t index {}; index < counter_names.size(); ++index)
            if (counter_names[index] == name)
                return static_cast<counter_id>(index);
        return {};
    }

    /// @brief All operational counters and metrics collected during search.
    ///
    /// @details
    /// `solver_statistics` is the single accumulator every hot-path subsystem reports through: `inc`/`add` update
    /// one `counter_id` (conflicts, decisions, propagations, restarts, learned clauses, and every other tracked
    /// metric), `snapshot_of` captures an immutable `snapshot` for exposure through
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
            counter_t conflicts {};
            /// @brief Total number of branching decisions made so far.
            counter_t decisions {};
            /// @brief Total number of unit propagations performed so far.
            counter_t propagations {};
            /// @brief Total number of restarts performed so far.
            counter_t restarts {};
            /// @brief Total number of clauses learned from conflict analysis so far.
            counter_t learned_clauses {};
            /// @brief Sum of learned-clause LBD/glue values observed so far.
            counter_t learned_clause_glue_total {};
            /// @brief Number of learned-clause LBD/glue samples included in the total.
            counter_t learned_clause_glue_samples {};
            counter_t reduction_passes {};
            counter_t reduced_clauses {};
            counter_t deleted_clauses {};
            counter_t probes {};
            counter_t probe_units {};
            counter_t walk_flips {};
            /// @brief Total number of terminate-callback polls executed so far.
            counter_t terminate_callback_calls {};
            /// @brief Total number of learned-clause callback invocations executed so far.
            counter_t learn_callback_calls {};
            /// @brief Total number of external propagator callback invocations executed so far.
            counter_t external_propagator_calls {};
            /// @brief Total number of recognized option updates applied through the facade.
            counter_t option_updates {};
            /// @brief Total number of recognized configuration profile updates applied through the facade.
            counter_t configuration_updates {};
        };

        /// @brief Constructs a statistics accumulator with every counter at zero.
        /// @throws None (noexcept).
        solver_statistics() noexcept = default;

        /// @brief Increments one counter by one.
        /// @param id Counter to increment.
        /// @throws None (noexcept).
        void inc(const counter_id id) noexcept { add(id, 1u); }

        /// @brief Adds a given amount to one counter.
        /// @param id Counter to update.
        /// @param amount Amount to add.
        /// @throws None (noexcept).
        void add(const counter_id id, const counter_t amount) noexcept;

        /// @brief Captures an immutable snapshot of the currently tracked counters.
        /// @return Point-in-time copy of every tracked counter.
        /// @throws None (noexcept).
        snapshot snapshot_of() const noexcept { return snapshot_; }

        /// @brief Reports whether every tracked counter is monotonic between two snapshots.
        /// @param before Baseline snapshot.
        /// @param after Candidate snapshot.
        /// @return True when all counters in @p after are greater-than-or-equal to @p before.
        /// @throws None (noexcept).
        static bool snapshot_monotonic(const snapshot& before, const snapshot& after) noexcept;

        /// @brief Computes the non-negative per-counter delta between two snapshots.
        /// @param before Baseline snapshot.
        /// @param after Candidate snapshot.
        /// @return Snapshot containing `after - before` for each counter (saturating at zero).
        /// @throws None (noexcept).
        static snapshot snapshot_delta_between(const snapshot& before, const snapshot& after) noexcept;

        /// @brief Merges counters accumulated by a sub-phase (portfolio worker, parallel sub-task) into this instance.
        /// @param other Statistics accumulator whose counters should be merged in.
        /// @throws None (noexcept).
        void merge_phase_statistics(const solver_statistics& other) noexcept;

        /// @brief Resets per-epoch counters at an incremental session boundary.
        /// @throws None (noexcept).
        void reset_epoch_counters() noexcept;

    private:
        snapshot snapshot_ {};
    };
}
