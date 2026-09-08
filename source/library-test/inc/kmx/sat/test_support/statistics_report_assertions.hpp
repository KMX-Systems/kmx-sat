/// @file inc/kmx/sat/test_support/statistics_report_assertions.hpp
/// @brief Shared assertions over parsed solver statistics reports.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/telemetry/solver_statistics.hpp>
#include <kmx/sat/test_support/statistics_report_parser.hpp>

namespace kmx::sat::test_support
{
    inline constexpr std::size_t compact_statistics_report_field_count = 10u;
    inline constexpr std::size_t verbose_statistics_report_field_count = 15u;

    inline bool compact_statistics_report_matches_snapshot(const statistics_report_map_t& parsed,
                                                           const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return parsed.contains(telemetry::counter_id::conflicts) && parsed.contains(telemetry::counter_id::decisions) &&
               parsed.contains(telemetry::counter_id::propagations) && parsed.contains(telemetry::counter_id::restarts) &&
               parsed.contains(telemetry::counter_id::learned_clauses) &&
               (parsed.at(telemetry::counter_id::conflicts) == snapshot.conflicts) &&
               (parsed.at(telemetry::counter_id::decisions) == snapshot.decisions) &&
               (parsed.at(telemetry::counter_id::propagations) == snapshot.propagations) &&
               (parsed.at(telemetry::counter_id::restarts) == snapshot.restarts) &&
               (parsed.at(telemetry::counter_id::learned_clauses) == snapshot.learned_clauses) &&
               parsed.contains(telemetry::counter_id::learned_clause_glue_total) &&
               parsed.contains(telemetry::counter_id::learned_clause_glue_samples) &&
               (parsed.at(telemetry::counter_id::learned_clause_glue_total) == snapshot.learned_clause_glue_total) &&
               (parsed.at(telemetry::counter_id::learned_clause_glue_samples) == snapshot.learned_clause_glue_samples) &&
               parsed.contains(telemetry::counter_id::reduction_passes) && parsed.contains(telemetry::counter_id::reduced_clauses) &&
               parsed.contains(telemetry::counter_id::deleted_clauses) &&
               (parsed.at(telemetry::counter_id::reduction_passes) == snapshot.reduction_passes) &&
               (parsed.at(telemetry::counter_id::reduced_clauses) == snapshot.reduced_clauses) &&
               (parsed.at(telemetry::counter_id::deleted_clauses) == snapshot.deleted_clauses);
    }

    inline bool verbose_statistics_report_matches_snapshot(const statistics_report_map_t& parsed,
                                                           const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return compact_statistics_report_matches_snapshot(parsed, snapshot) &&
               parsed.contains(telemetry::counter_id::terminate_callback_calls) &&
               parsed.contains(telemetry::counter_id::learn_callback_calls) &&
               parsed.contains(telemetry::counter_id::external_propagator_calls) &&
               parsed.contains(telemetry::counter_id::option_updates) && parsed.contains(telemetry::counter_id::configuration_updates) &&
               (parsed.at(telemetry::counter_id::terminate_callback_calls) == snapshot.terminate_callback_calls) &&
               (parsed.at(telemetry::counter_id::learn_callback_calls) == snapshot.learn_callback_calls) &&
               (parsed.at(telemetry::counter_id::external_propagator_calls) == snapshot.external_propagator_calls) &&
               (parsed.at(telemetry::counter_id::option_updates) == snapshot.option_updates) &&
               (parsed.at(telemetry::counter_id::configuration_updates) == snapshot.configuration_updates);
    }

    inline bool compact_statistics_report_exactly_matches_snapshot(const statistics_report_map_t& parsed,
                                                                   const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return (parsed.size() == compact_statistics_report_field_count) && compact_statistics_report_matches_snapshot(parsed, snapshot);
    }

    inline bool verbose_statistics_report_exactly_matches_snapshot(const statistics_report_map_t& parsed,
                                                                   const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return (parsed.size() == verbose_statistics_report_field_count) && verbose_statistics_report_matches_snapshot(parsed, snapshot);
    }

    inline bool compact_statistics_report_has_compact_schema(const statistics_report_map_t& parsed) noexcept
    {
        return (parsed.size() == compact_statistics_report_field_count) && parsed.contains(telemetry::counter_id::conflicts) &&
               parsed.contains(telemetry::counter_id::decisions) && parsed.contains(telemetry::counter_id::propagations) &&
               parsed.contains(telemetry::counter_id::restarts) && parsed.contains(telemetry::counter_id::learned_clauses) &&
               parsed.contains(telemetry::counter_id::learned_clause_glue_total) &&
               parsed.contains(telemetry::counter_id::learned_clause_glue_samples);
    }

    inline bool verbose_statistics_report_has_verbose_schema(const statistics_report_map_t& parsed) noexcept
    {
        return (parsed.size() == verbose_statistics_report_field_count) && parsed.contains(telemetry::counter_id::conflicts) &&
               parsed.contains(telemetry::counter_id::decisions) && parsed.contains(telemetry::counter_id::propagations) &&
               parsed.contains(telemetry::counter_id::restarts) && parsed.contains(telemetry::counter_id::learned_clauses) &&
               parsed.contains(telemetry::counter_id::learned_clause_glue_total) &&
               parsed.contains(telemetry::counter_id::learned_clause_glue_samples) &&
               parsed.contains(telemetry::counter_id::terminate_callback_calls) &&
               parsed.contains(telemetry::counter_id::learn_callback_calls) &&
               parsed.contains(telemetry::counter_id::external_propagator_calls) &&
               parsed.contains(telemetry::counter_id::option_updates) && parsed.contains(telemetry::counter_id::configuration_updates);
    }

    inline bool verbose_statistics_report_extends_compact_consistently(const statistics_report_map_t& compact,
                                                                       const statistics_report_map_t& verbose) noexcept
    {
        if (!compact_statistics_report_has_compact_schema(compact) || !verbose_statistics_report_has_verbose_schema(verbose))
            return false;
        return (verbose.at(telemetry::counter_id::conflicts) == compact.at(telemetry::counter_id::conflicts)) &&
               (verbose.at(telemetry::counter_id::decisions) == compact.at(telemetry::counter_id::decisions)) &&
               (verbose.at(telemetry::counter_id::propagations) == compact.at(telemetry::counter_id::propagations)) &&
               (verbose.at(telemetry::counter_id::restarts) == compact.at(telemetry::counter_id::restarts)) &&
               (verbose.at(telemetry::counter_id::learned_clauses) == compact.at(telemetry::counter_id::learned_clauses));
    }

    inline bool compact_statistics_report_delta_matches_snapshot_delta(const statistics_report_map_t& before,
                                                                       const statistics_report_map_t& after,
                                                                       const telemetry::solver_statistics::snapshot& delta) noexcept
    {
        if (!compact_statistics_report_has_compact_schema(before) || !compact_statistics_report_has_compact_schema(after))
            return false;

        const auto saturating_delta = [](const std::uint64_t before_value, const std::uint64_t after_value) noexcept
        { return (after_value >= before_value) ? after_value - before_value : 0u; };

        return (saturating_delta(before.at(telemetry::counter_id::conflicts), after.at(telemetry::counter_id::conflicts)) ==
                delta.conflicts) &&
               (saturating_delta(before.at(telemetry::counter_id::decisions), after.at(telemetry::counter_id::decisions)) ==
                delta.decisions) &&
               (saturating_delta(before.at(telemetry::counter_id::propagations), after.at(telemetry::counter_id::propagations)) ==
                delta.propagations) &&
               (saturating_delta(before.at(telemetry::counter_id::restarts), after.at(telemetry::counter_id::restarts)) ==
                delta.restarts) &&
               (saturating_delta(before.at(telemetry::counter_id::learned_clauses), after.at(telemetry::counter_id::learned_clauses)) ==
                delta.learned_clauses);
    }

    inline bool verbose_statistics_report_delta_matches_snapshot_delta(const statistics_report_map_t& before,
                                                                       const statistics_report_map_t& after,
                                                                       const telemetry::solver_statistics::snapshot& delta) noexcept
    {
        if (!verbose_statistics_report_has_verbose_schema(before) || !verbose_statistics_report_has_verbose_schema(after))
            return false;

        const auto saturating_delta = [](const std::uint64_t before_value, const std::uint64_t after_value) noexcept
        { return (after_value >= before_value) ? after_value - before_value : 0u; };

        return (saturating_delta(before.at(telemetry::counter_id::conflicts), after.at(telemetry::counter_id::conflicts)) ==
                delta.conflicts) &&
               (saturating_delta(before.at(telemetry::counter_id::decisions), after.at(telemetry::counter_id::decisions)) ==
                delta.decisions) &&
               (saturating_delta(before.at(telemetry::counter_id::propagations), after.at(telemetry::counter_id::propagations)) ==
                delta.propagations) &&
               (saturating_delta(before.at(telemetry::counter_id::restarts), after.at(telemetry::counter_id::restarts)) ==
                delta.restarts) &&
               (saturating_delta(before.at(telemetry::counter_id::learned_clauses), after.at(telemetry::counter_id::learned_clauses)) ==
                delta.learned_clauses) &&
               saturating_delta(before.at(telemetry::counter_id::terminate_callback_calls),
                                after.at(telemetry::counter_id::terminate_callback_calls)) == delta.terminate_callback_calls &&
               (saturating_delta(before.at(telemetry::counter_id::learn_callback_calls),
                                 after.at(telemetry::counter_id::learn_callback_calls)) == delta.learn_callback_calls) &&
               saturating_delta(before.at(telemetry::counter_id::external_propagator_calls),
                                after.at(telemetry::counter_id::external_propagator_calls)) == delta.external_propagator_calls &&
               (saturating_delta(before.at(telemetry::counter_id::option_updates), after.at(telemetry::counter_id::option_updates)) ==
                delta.option_updates) &&
               (saturating_delta(before.at(telemetry::counter_id::configuration_updates),
                                 after.at(telemetry::counter_id::configuration_updates)) == delta.configuration_updates);
    }
}
