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

    inline bool compact_statistics_report_matches_snapshot(const statistics_report_map& parsed,
                                                           const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return parsed.contains("conflicts") && parsed.contains("decisions") && parsed.contains("propagations") &&
               parsed.contains("restarts") && parsed.contains("learned_clauses") && parsed.at("conflicts") == snapshot.conflicts &&
               parsed.at("decisions") == snapshot.decisions && parsed.at("propagations") == snapshot.propagations &&
               parsed.at("restarts") == snapshot.restarts && parsed.at("learned_clauses") == snapshot.learned_clauses &&
               parsed.contains("learned_clause_glue_total") && parsed.contains("learned_clause_glue_samples") &&
               parsed.at("learned_clause_glue_total") == snapshot.learned_clause_glue_total &&
               parsed.at("learned_clause_glue_samples") == snapshot.learned_clause_glue_samples && parsed.contains("reduction_passes") &&
               parsed.contains("reduced_clauses") && parsed.contains("deleted_clauses") &&
               parsed.at("reduction_passes") == snapshot.reduction_passes && parsed.at("reduced_clauses") == snapshot.reduced_clauses &&
               parsed.at("deleted_clauses") == snapshot.deleted_clauses;
    }

    inline bool verbose_statistics_report_matches_snapshot(const statistics_report_map& parsed,
                                                           const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return compact_statistics_report_matches_snapshot(parsed, snapshot) && parsed.contains("terminate_callback_calls") &&
               parsed.contains("learn_callback_calls") && parsed.contains("external_propagator_calls") &&
               parsed.contains("option_updates") && parsed.contains("configuration_updates") &&
               parsed.at("terminate_callback_calls") == snapshot.terminate_callback_calls &&
               parsed.at("learn_callback_calls") == snapshot.learn_callback_calls &&
               parsed.at("external_propagator_calls") == snapshot.external_propagator_calls &&
               parsed.at("option_updates") == snapshot.option_updates &&
               parsed.at("configuration_updates") == snapshot.configuration_updates;
    }

    inline bool compact_statistics_report_exactly_matches_snapshot(const statistics_report_map& parsed,
                                                                   const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return parsed.size() == compact_statistics_report_field_count && compact_statistics_report_matches_snapshot(parsed, snapshot);
    }

    inline bool verbose_statistics_report_exactly_matches_snapshot(const statistics_report_map& parsed,
                                                                   const telemetry::solver_statistics::snapshot& snapshot) noexcept
    {
        return parsed.size() == verbose_statistics_report_field_count && verbose_statistics_report_matches_snapshot(parsed, snapshot);
    }

    inline bool compact_statistics_report_has_compact_schema(const statistics_report_map& parsed) noexcept
    {
        return parsed.size() == compact_statistics_report_field_count && parsed.contains("conflicts") && parsed.contains("decisions") &&
               parsed.contains("propagations") && parsed.contains("restarts") && parsed.contains("learned_clauses") &&
               parsed.contains("learned_clause_glue_total") && parsed.contains("learned_clause_glue_samples");
    }

    inline bool verbose_statistics_report_has_verbose_schema(const statistics_report_map& parsed) noexcept
    {
        return parsed.size() == verbose_statistics_report_field_count && parsed.contains("conflicts") && parsed.contains("decisions") &&
               parsed.contains("propagations") && parsed.contains("restarts") && parsed.contains("learned_clauses") &&
               parsed.contains("learned_clause_glue_total") && parsed.contains("learned_clause_glue_samples") &&
               parsed.contains("terminate_callback_calls") && parsed.contains("learn_callback_calls") &&
               parsed.contains("external_propagator_calls") && parsed.contains("option_updates") &&
               parsed.contains("configuration_updates");
    }

    inline bool verbose_statistics_report_extends_compact_consistently(const statistics_report_map& compact,
                                                                       const statistics_report_map& verbose) noexcept
    {
        if (!compact_statistics_report_has_compact_schema(compact) || !verbose_statistics_report_has_verbose_schema(verbose))
            return false;
        return verbose.at("conflicts") == compact.at("conflicts") && verbose.at("decisions") == compact.at("decisions") &&
               verbose.at("propagations") == compact.at("propagations") && verbose.at("restarts") == compact.at("restarts") &&
               verbose.at("learned_clauses") == compact.at("learned_clauses");
    }

    inline bool compact_statistics_report_delta_matches_snapshot_delta(const statistics_report_map& before,
                                                                       const statistics_report_map& after,
                                                                       const telemetry::solver_statistics::snapshot& delta) noexcept
    {
        if (!compact_statistics_report_has_compact_schema(before) || !compact_statistics_report_has_compact_schema(after))
            return false;

        const auto saturating_delta = [](const std::uint64_t before_value, const std::uint64_t after_value) noexcept
        { return after_value >= before_value ? after_value - before_value : 0u; };

        return saturating_delta(before.at("conflicts"), after.at("conflicts")) == delta.conflicts &&
               saturating_delta(before.at("decisions"), after.at("decisions")) == delta.decisions &&
               saturating_delta(before.at("propagations"), after.at("propagations")) == delta.propagations &&
               saturating_delta(before.at("restarts"), after.at("restarts")) == delta.restarts &&
               saturating_delta(before.at("learned_clauses"), after.at("learned_clauses")) == delta.learned_clauses;
    }

    inline bool verbose_statistics_report_delta_matches_snapshot_delta(const statistics_report_map& before,
                                                                       const statistics_report_map& after,
                                                                       const telemetry::solver_statistics::snapshot& delta) noexcept
    {
        if (!verbose_statistics_report_has_verbose_schema(before) || !verbose_statistics_report_has_verbose_schema(after))
            return false;

        const auto saturating_delta = [](const std::uint64_t before_value, const std::uint64_t after_value) noexcept
        { return after_value >= before_value ? after_value - before_value : 0u; };

        return saturating_delta(before.at("conflicts"), after.at("conflicts")) == delta.conflicts &&
               saturating_delta(before.at("decisions"), after.at("decisions")) == delta.decisions &&
               saturating_delta(before.at("propagations"), after.at("propagations")) == delta.propagations &&
               saturating_delta(before.at("restarts"), after.at("restarts")) == delta.restarts &&
               saturating_delta(before.at("learned_clauses"), after.at("learned_clauses")) == delta.learned_clauses &&
               saturating_delta(before.at("terminate_callback_calls"), after.at("terminate_callback_calls")) ==
                   delta.terminate_callback_calls &&
               saturating_delta(before.at("learn_callback_calls"), after.at("learn_callback_calls")) == delta.learn_callback_calls &&
               saturating_delta(before.at("external_propagator_calls"), after.at("external_propagator_calls")) ==
                   delta.external_propagator_calls &&
               saturating_delta(before.at("option_updates"), after.at("option_updates")) == delta.option_updates &&
               saturating_delta(before.at("configuration_updates"), after.at("configuration_updates")) == delta.configuration_updates;
    }
}
