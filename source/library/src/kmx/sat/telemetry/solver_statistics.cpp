/// @file library/src/kmx/sat/telemetry/solver_statistics.cpp
/// @brief Out-of-line definitions declared by kmx/sat/telemetry/solver_statistics.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/telemetry/solver_statistics.hpp>

namespace kmx::sat::telemetry
{
    void solver_statistics::add(const counter_id id, const counter_t amount) noexcept
    {
        switch (id)
        {
            case counter_id::conflicts:
                snapshot_.conflicts += amount;
                break;
            case counter_id::decisions:
                snapshot_.decisions += amount;
                break;
            case counter_id::propagations:
                snapshot_.propagations += amount;
                break;
            case counter_id::restarts:
                snapshot_.restarts += amount;
                break;
            case counter_id::learned_clauses:
                snapshot_.learned_clauses += amount;
                break;
            case counter_id::learned_clause_glue_total:
                snapshot_.learned_clause_glue_total += amount;
                break;
            case counter_id::learned_clause_glue_samples:
                snapshot_.learned_clause_glue_samples += amount;
                break;
            case counter_id::reduction_passes:
                snapshot_.reduction_passes += amount;
                break;
            case counter_id::reduced_clauses:
                snapshot_.reduced_clauses += amount;
                break;
            case counter_id::deleted_clauses:
                snapshot_.deleted_clauses += amount;
                break;
            case counter_id::probes:
                snapshot_.probes += amount;
                break;
            case counter_id::probe_units:
                snapshot_.probe_units += amount;
                break;
            case counter_id::walk_flips:
                snapshot_.walk_flips += amount;
                break;
            case counter_id::terminate_callback_calls:
                snapshot_.terminate_callback_calls += amount;
                break;
            case counter_id::learn_callback_calls:
                snapshot_.learn_callback_calls += amount;
                break;
            case counter_id::external_propagator_calls:
                snapshot_.external_propagator_calls += amount;
                break;
            case counter_id::option_updates:
                snapshot_.option_updates += amount;
                break;
            case counter_id::configuration_updates:
                snapshot_.configuration_updates += amount;
                break;
        }
    }

    bool solver_statistics::snapshot_monotonic(const snapshot& before, const snapshot& after) noexcept
    {
        return (after.conflicts >= before.conflicts) && (after.decisions >= before.decisions) &&
               (after.propagations >= before.propagations) && (after.restarts >= before.restarts) &&
               (after.learned_clauses >= before.learned_clauses) && (after.learned_clause_glue_total >= before.learned_clause_glue_total) &&
               (after.learned_clause_glue_samples >= before.learned_clause_glue_samples) &&
               (after.reduction_passes >= before.reduction_passes) && (after.reduced_clauses >= before.reduced_clauses) &&
               (after.deleted_clauses >= before.deleted_clauses) && (after.terminate_callback_calls >= before.terminate_callback_calls) &&
               (after.learn_callback_calls >= before.learn_callback_calls) &&
               (after.external_propagator_calls >= before.external_propagator_calls) && (after.option_updates >= before.option_updates) &&
               (after.configuration_updates >= before.configuration_updates);
    }

    solver_statistics::snapshot solver_statistics::snapshot_delta_between(const snapshot& before, const snapshot& after) noexcept
    {
        return snapshot {
            .conflicts = (after.conflicts >= before.conflicts) ? after.conflicts - before.conflicts : 0u,
            .decisions = (after.decisions >= before.decisions) ? after.decisions - before.decisions : 0u,
            .propagations = (after.propagations >= before.propagations) ? after.propagations - before.propagations : 0u,
            .restarts = (after.restarts >= before.restarts) ? after.restarts - before.restarts : 0u,
            .learned_clauses = (after.learned_clauses >= before.learned_clauses) ? after.learned_clauses - before.learned_clauses : 0u,
            .learned_clause_glue_total = (after.learned_clause_glue_total >= before.learned_clause_glue_total) ?
                                             after.learned_clause_glue_total - before.learned_clause_glue_total :
                                             0u,
            .learned_clause_glue_samples = (after.learned_clause_glue_samples >= before.learned_clause_glue_samples) ?
                                               after.learned_clause_glue_samples - before.learned_clause_glue_samples :
                                               0u,
            .reduction_passes = (after.reduction_passes >= before.reduction_passes) ? after.reduction_passes - before.reduction_passes : 0u,
            .reduced_clauses = (after.reduced_clauses >= before.reduced_clauses) ? after.reduced_clauses - before.reduced_clauses : 0u,
            .deleted_clauses = (after.deleted_clauses >= before.deleted_clauses) ? after.deleted_clauses - before.deleted_clauses : 0u,
            .terminate_callback_calls = (after.terminate_callback_calls >= before.terminate_callback_calls) ?
                                            after.terminate_callback_calls - before.terminate_callback_calls :
                                            0u,
            .learn_callback_calls =
                (after.learn_callback_calls >= before.learn_callback_calls) ? after.learn_callback_calls - before.learn_callback_calls : 0u,
            .external_propagator_calls = (after.external_propagator_calls >= before.external_propagator_calls) ?
                                             after.external_propagator_calls - before.external_propagator_calls :
                                             0u,
            .option_updates = (after.option_updates >= before.option_updates) ? after.option_updates - before.option_updates : 0u,
            .configuration_updates = (after.configuration_updates >= before.configuration_updates) ?
                                         after.configuration_updates - before.configuration_updates :
                                         0u,
        };
    }

    void solver_statistics::merge_phase_statistics(const solver_statistics& other) noexcept
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

    void solver_statistics::reset_epoch_counters() noexcept
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
}
