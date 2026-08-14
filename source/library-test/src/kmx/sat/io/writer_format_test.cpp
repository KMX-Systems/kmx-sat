#include <catch2/catch_test_macros.hpp>

#include <string>

#include <kmx/sat/io/writer/format.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>
#include <kmx/sat/test_support/statistics_report_assertions.hpp>
#include <kmx/sat/test_support/statistics_report_parser.hpp>

namespace kmx::sat::io::writer
{
    TEST_CASE("writer format serializes single-character report lines", "[sat]")
    {
        format formatter;

        formatter.write_report_line('c');
        formatter.write_report_line('\n');

        REQUIRE(formatter.buffer_view() == "c\n\n");
    }

    TEST_CASE("writer format serializes compact and verbose statistics", "[sat]")
    {
        format formatter;

        telemetry::solver_statistics::snapshot snapshot {};
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

        formatter.write_statistics(snapshot);
        const auto compact = std::string {formatter.buffer_view()};
        const auto compact_parsed = test_support::parse_statistics_report_line(compact);
        REQUIRE(test_support::compact_statistics_report_exactly_matches_snapshot(compact_parsed, snapshot));
        REQUIRE(compact.find("option_updates=") == std::string::npos);
        REQUIRE(compact.ends_with('\n'));

        formatter.clear();
        formatter.write_statistics(snapshot, format::statistics_detail::verbose);
        const auto verbose = std::string {formatter.buffer_view()};
        const auto verbose_parsed = test_support::parse_statistics_report_line(verbose);
        REQUIRE(test_support::verbose_statistics_report_exactly_matches_snapshot(verbose_parsed, snapshot));
        REQUIRE(verbose.ends_with('\n'));
    }
}
