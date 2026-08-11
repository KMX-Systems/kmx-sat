#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/telemetry/report_formatter.hpp>

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

        const auto stats_line = formatter.format_statistics_line(snapshot);
        const auto resource_line = formatter.format_resource_line();
        const auto progress_line = formatter.format_progress();

        REQUIRE(stats_line.find("conflicts=3") != std::string::npos);
        REQUIRE(resource_line.find("time=") != std::string::npos);
        REQUIRE(progress_line.find("progress: step 1 of 10") != std::string::npos);
        REQUIRE(formatter.progress_steps() == 1u);
    }
}
