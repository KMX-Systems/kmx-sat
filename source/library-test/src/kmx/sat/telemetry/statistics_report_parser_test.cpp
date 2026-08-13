#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/telemetry/solver_statistics.hpp>
#include <kmx/sat/test_support/statistics_report_assertions.hpp>
#include <kmx/sat/test_support/statistics_report_parser.hpp>

namespace kmx::sat::telemetry
{
    TEST_CASE("statistics report parser handles malformed tokens and assertion helpers", "[sat]")
    {
        solver_statistics::snapshot snapshot {};
        snapshot.conflicts = 3u;
        snapshot.decisions = 7u;
        snapshot.propagations = 11u;
        snapshot.restarts = 2u;
        snapshot.learned_clauses = 5u;
        snapshot.learned_clause_glue_total = 9u;
        snapshot.learned_clause_glue_samples = 3u;
        snapshot.reduction_passes = 2u;
        snapshot.reduced_clauses = 4u;
        snapshot.deleted_clauses = 3u;
        snapshot.terminate_callback_calls = 13u;
        snapshot.learn_callback_calls = 17u;
        snapshot.external_propagator_calls = 19u;
        snapshot.option_updates = 23u;
        snapshot.configuration_updates = 29u;

        const auto compact_line =
            "junk conflicts=3 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3 malformed=abc orphan "
            "extra=0";
        const auto compact_parsed = test_support::parse_statistics_report_line(compact_line);
        REQUIRE(test_support::compact_statistics_report_matches_snapshot(compact_parsed, snapshot));
        REQUIRE_FALSE(test_support::compact_statistics_report_exactly_matches_snapshot(compact_parsed, snapshot));

        const auto verbose_line =
            "conflicts=3 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3 terminate_callback_calls=13 "
            "learn_callback_calls=17 external_propagator_calls=19 option_updates=23 configuration_updates=29 bad";
        const auto verbose_parsed = test_support::parse_statistics_report_line(verbose_line);
        REQUIRE(test_support::verbose_statistics_report_matches_snapshot(verbose_parsed, snapshot));
        REQUIRE(test_support::verbose_statistics_report_exactly_matches_snapshot(verbose_parsed, snapshot));
        REQUIRE(test_support::compact_statistics_report_has_compact_schema(
            test_support::parse_statistics_report_line(
                "conflicts=3 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3")));
        REQUIRE(test_support::verbose_statistics_report_has_verbose_schema(verbose_parsed));
        REQUIRE(test_support::verbose_statistics_report_extends_compact_consistently(
            test_support::parse_statistics_report_line(
                "conflicts=3 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3"),
            verbose_parsed));

        const auto compact_before =
            test_support::parse_statistics_report_line("conflicts=3 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3");
        const auto compact_after =
            test_support::parse_statistics_report_line("conflicts=4 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3");
        const auto compact_delta = solver_statistics::snapshot {
            .conflicts = 1u,
            .decisions = 0u,
            .propagations = 0u,
            .restarts = 0u,
            .learned_clauses = 0u,
        };
        REQUIRE(test_support::compact_statistics_report_delta_matches_snapshot_delta(
            compact_before,
            compact_after,
            compact_delta));
        const auto wrong_compact_delta = solver_statistics::snapshot {
            .conflicts = 2u,
            .decisions = 0u,
            .propagations = 0u,
            .restarts = 0u,
            .learned_clauses = 0u,
        };
        REQUIRE_FALSE(test_support::compact_statistics_report_delta_matches_snapshot_delta(
            compact_before,
            compact_after,
            wrong_compact_delta));

        const auto verbose_before =
            test_support::parse_statistics_report_line(
                "conflicts=3 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3 terminate_callback_calls=13 "
                "learn_callback_calls=17 external_propagator_calls=19 option_updates=23 configuration_updates=29");
        const auto verbose_after =
            test_support::parse_statistics_report_line(
                "conflicts=4 decisions=7 propagations=11 restarts=2 learned_clauses=5 learned_clause_glue_total=9 learned_clause_glue_samples=3 reduction_passes=2 reduced_clauses=4 deleted_clauses=3 terminate_callback_calls=13 "
                "learn_callback_calls=18 external_propagator_calls=19 option_updates=23 configuration_updates=29");
        const auto verbose_delta = solver_statistics::snapshot {
            .conflicts = 1u,
            .decisions = 0u,
            .propagations = 0u,
            .restarts = 0u,
            .learned_clauses = 0u,
            .terminate_callback_calls = 0u,
            .learn_callback_calls = 1u,
            .external_propagator_calls = 0u,
            .option_updates = 0u,
            .configuration_updates = 0u,
        };
        REQUIRE(test_support::verbose_statistics_report_delta_matches_snapshot_delta(
            verbose_before,
            verbose_after,
            verbose_delta));
        const auto wrong_verbose_delta = solver_statistics::snapshot {
            .conflicts = 1u,
            .decisions = 0u,
            .propagations = 0u,
            .restarts = 0u,
            .learned_clauses = 0u,
            .terminate_callback_calls = 1u,
            .learn_callback_calls = 1u,
            .external_propagator_calls = 0u,
            .option_updates = 0u,
            .configuration_updates = 0u,
        };
        REQUIRE_FALSE(test_support::verbose_statistics_report_delta_matches_snapshot_delta(
            verbose_before,
            verbose_after,
            wrong_verbose_delta));

        const auto missing_key_line =
            "conflicts=3 decisions=7 propagations=11 restarts=2 terminate_callback_calls=13 learn_callback_calls=17 "
            "external_propagator_calls=19 option_updates=23 configuration_updates=29";
        const auto missing_key_parsed = test_support::parse_statistics_report_line(missing_key_line);
        REQUIRE_FALSE(test_support::compact_statistics_report_matches_snapshot(missing_key_parsed, snapshot));
        REQUIRE_FALSE(test_support::verbose_statistics_report_matches_snapshot(missing_key_parsed, snapshot));
    }
}
