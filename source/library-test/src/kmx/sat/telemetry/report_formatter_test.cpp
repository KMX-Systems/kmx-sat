#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/telemetry/report_formatter.hpp>
#include <kmx/sat/test_support/statistics_report_assertions.hpp>
#include <kmx/sat/test_support/statistics_report_parser.hpp>

namespace kmx::sat::telemetry
{
    TEST_CASE("report formatter renders progress and resource summaries", "[sat]")
    {
        report_formatter formatter;
        solver_statistics::snapshot snapshot {};
        snapshot.conflicts = 3u;
        snapshot.decisions = 7u;
        snapshot.propagations = 11u;
        snapshot.restarts = 2u;
        snapshot.learned_clauses = 5u;
        snapshot.terminate_callback_calls = 13u;
        snapshot.learn_callback_calls = 17u;
        snapshot.external_propagator_calls = 19u;
        snapshot.option_updates = 23u;
        snapshot.configuration_updates = 29u;

        const auto stats_line = formatter.format_statistics_line(snapshot);
        const auto verbose_stats_line = formatter.format_statistics_line_verbose(snapshot);
        const auto resource_line = formatter.format_resource_line();
        const auto progress_line = formatter.format_progress();
        const auto compact_parsed = test_support::parse_statistics_report_line(stats_line);
        const auto verbose_parsed = test_support::parse_statistics_report_line(verbose_stats_line);

        REQUIRE(test_support::compact_statistics_report_exactly_matches_snapshot(compact_parsed, snapshot));
        REQUIRE(test_support::verbose_statistics_report_exactly_matches_snapshot(verbose_parsed, snapshot));
        REQUIRE(resource_line.find("time=") != std::string::npos);
        REQUIRE(progress_line.find("progress: step 1 of 10") != std::string::npos);
        REQUIRE(formatter.progress_steps() == 1u);
    }
}
