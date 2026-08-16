/// @file api/kmx/sat/telemetry/solver_statistics.hpp
/// @brief All operational counters and metrics collected during search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <string>
    #include <string_view>
#endif
#include <kmx/sat/counter.hpp>

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

        /// @brief Increments a named counter by one.
        /// @param counter_name Identifier of the counter to increment.
        /// @throws None (noexcept).
        void inc(const std::string_view counter_name) noexcept { add(counter_name, 1u); }

        /// @brief Adds a given amount to a named counter.
        /// @param counter_name Identifier of the counter to update.
        /// @param amount Amount to add.
        /// @throws None (noexcept).
        void add(const std::string_view counter_name, const counter_t amount) noexcept
        {
            switch (counter_name.size())
            {
                case 8u:
                    if (counter_name == "restarts")
                        snapshot_.restarts += amount;
                    break;
                case 9u:
                    if (counter_name == "conflicts")
                        snapshot_.conflicts += amount;
                    else if (counter_name == "decisions")
                        snapshot_.decisions += amount;
                    break;
                case 12u:
                    if (counter_name == "propagations")
                        snapshot_.propagations += amount;
                    break;
                case 14u:
                    if (counter_name == "option_updates")
                        snapshot_.option_updates += amount;
                    break;
                case 15u:
                    if (counter_name == "learned_clauses")
                        snapshot_.learned_clauses += amount;
                    else if (counter_name == "reduced_clauses")
                        snapshot_.reduced_clauses += amount;
                    else if (counter_name == "deleted_clauses")
                        snapshot_.deleted_clauses += amount;
                    break;
                case 16u:
                    if (counter_name == "reduction_passes")
                        snapshot_.reduction_passes += amount;
                    break;
                case 20u:
                    if (counter_name == "learn_callback_calls")
                        snapshot_.learn_callback_calls += amount;
                    break;
                case 21u:
                    if (counter_name == "configuration_updates")
                        snapshot_.configuration_updates += amount;
                    break;
                case 24u:
                    if (counter_name == "terminate_callback_calls")
                        snapshot_.terminate_callback_calls += amount;
                    break;
                case 25u:
                    if (counter_name == "learned_clause_glue_total")
                        snapshot_.learned_clause_glue_total += amount;
                    else if (counter_name == "external_propagator_calls")
                        snapshot_.external_propagator_calls += amount;
                    break;
                case 27u:
                    if (counter_name == "learned_clause_glue_samples")
                        snapshot_.learned_clause_glue_samples += amount;
                    break;
                default:
                    break;
            }
        }

        /// @brief Captures an immutable snapshot of the currently tracked counters.
        /// @return Point-in-time copy of every tracked counter.
        /// @throws None (noexcept).
        snapshot snapshot_of() const noexcept { return snapshot_; }

        /// @brief Reports whether every tracked counter is monotonic between two snapshots.
        /// @param before Baseline snapshot.
        /// @param after Candidate snapshot.
        /// @return True when all counters in @p after are greater-than-or-equal to @p before.
        /// @throws None (noexcept).
        static bool snapshot_monotonic(const snapshot& before, const snapshot& after) noexcept
        {
            return after.conflicts >= before.conflicts && after.decisions >= before.decisions &&
                   after.propagations >= before.propagations && after.restarts >= before.restarts &&
                   after.learned_clauses >= before.learned_clauses && after.learned_clause_glue_total >= before.learned_clause_glue_total &&
                   after.learned_clause_glue_samples >= before.learned_clause_glue_samples &&
                   after.reduction_passes >= before.reduction_passes && after.reduced_clauses >= before.reduced_clauses &&
                   after.deleted_clauses >= before.deleted_clauses && after.terminate_callback_calls >= before.terminate_callback_calls &&
                   after.learn_callback_calls >= before.learn_callback_calls &&
                   after.external_propagator_calls >= before.external_propagator_calls && after.option_updates >= before.option_updates &&
                   after.configuration_updates >= before.configuration_updates;
        }

        /// @brief Computes the non-negative per-counter delta between two snapshots.
        /// @param before Baseline snapshot.
        /// @param after Candidate snapshot.
        /// @return Snapshot containing `after - before` for each counter (saturating at zero).
        /// @throws None (noexcept).
        static snapshot snapshot_delta_between(const snapshot& before, const snapshot& after) noexcept
        {
            return snapshot {
                .conflicts = after.conflicts >= before.conflicts ? after.conflicts - before.conflicts : 0u,
                .decisions = after.decisions >= before.decisions ? after.decisions - before.decisions : 0u,
                .propagations = after.propagations >= before.propagations ? after.propagations - before.propagations : 0u,
                .restarts = after.restarts >= before.restarts ? after.restarts - before.restarts : 0u,
                .learned_clauses = after.learned_clauses >= before.learned_clauses ? after.learned_clauses - before.learned_clauses : 0u,
                .learned_clause_glue_total = after.learned_clause_glue_total >= before.learned_clause_glue_total ?
                                                 after.learned_clause_glue_total - before.learned_clause_glue_total :
                                                 0u,
                .learned_clause_glue_samples = after.learned_clause_glue_samples >= before.learned_clause_glue_samples ?
                                                   after.learned_clause_glue_samples - before.learned_clause_glue_samples :
                                                   0u,
                .reduction_passes =
                    after.reduction_passes >= before.reduction_passes ? after.reduction_passes - before.reduction_passes : 0u,
                .reduced_clauses = after.reduced_clauses >= before.reduced_clauses ? after.reduced_clauses - before.reduced_clauses : 0u,
                .deleted_clauses = after.deleted_clauses >= before.deleted_clauses ? after.deleted_clauses - before.deleted_clauses : 0u,
                .terminate_callback_calls = after.terminate_callback_calls >= before.terminate_callback_calls ?
                                                after.terminate_callback_calls - before.terminate_callback_calls :
                                                0u,
                .learn_callback_calls = after.learn_callback_calls >= before.learn_callback_calls ?
                                            after.learn_callback_calls - before.learn_callback_calls :
                                            0u,
                .external_propagator_calls = after.external_propagator_calls >= before.external_propagator_calls ?
                                                 after.external_propagator_calls - before.external_propagator_calls :
                                                 0u,
                .option_updates = after.option_updates >= before.option_updates ? after.option_updates - before.option_updates : 0u,
                .configuration_updates = after.configuration_updates >= before.configuration_updates ?
                                             after.configuration_updates - before.configuration_updates :
                                             0u,
            };
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
            snapshot_.learned_clause_glue_total += other.snapshot_.learned_clause_glue_total;
            snapshot_.learned_clause_glue_samples += other.snapshot_.learned_clause_glue_samples;
            snapshot_.reduction_passes += other.snapshot_.reduction_passes;
            snapshot_.reduced_clauses += other.snapshot_.reduced_clauses;
            snapshot_.deleted_clauses += other.snapshot_.deleted_clauses;
            snapshot_.terminate_callback_calls += other.snapshot_.terminate_callback_calls;
            snapshot_.learn_callback_calls += other.snapshot_.learn_callback_calls;
            snapshot_.external_propagator_calls += other.snapshot_.external_propagator_calls;
            snapshot_.option_updates += other.snapshot_.option_updates;
            snapshot_.configuration_updates += other.snapshot_.configuration_updates;
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
            snapshot_.learned_clause_glue_total = 0u;
            snapshot_.learned_clause_glue_samples = 0u;
            snapshot_.reduction_passes = 0u;
            snapshot_.reduced_clauses = 0u;
            snapshot_.deleted_clauses = 0u;
            snapshot_.terminate_callback_calls = 0u;
            snapshot_.learn_callback_calls = 0u;
            snapshot_.external_propagator_calls = 0u;
            snapshot_.option_updates = 0u;
            snapshot_.configuration_updates = 0u;
        }

    private:
        snapshot snapshot_ {};
    };
}
