#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/telemetry/logging_facade.hpp>
#include <kmx/sat/telemetry/profile_clock.hpp>
#include <kmx/sat/telemetry/report_formatter.hpp>
#include <kmx/sat/telemetry/solver_options.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("solver telemetry integration", "[sat]")
    {
        SECTION("logging facade records structured events")
        {
            telemetry::logging_facade logger;
            const literal lit {variable {7u}, false};
            const cdcl::clause::ref_t ref {11u};

            logger.log_clause(ref);
            logger.log_literal(lit);
            logger.log_gate();
            logger.log_extension();
            logger.log_phase_summary("search");

            REQUIRE(logger.event_count() == 5u);
            REQUIRE(logger.last_event().kind == telemetry::logging_facade::event_kind::phase_summary);
            REQUIRE(logger.last_event().phase_name == "search");
            REQUIRE(logger.last_event().ref_offset == ref.offset());
        }

        SECTION("telemetry options and statistics track state")
        {
            telemetry::solver_options options;
            options.set("max_conflicts", 128);
            REQUIRE(options.get("max_conflicts") == 128);
            REQUIRE(options.get("missing_option") == 0);

            const auto profile = options.load_profile("default");
            REQUIRE(profile.has_value());
            REQUIRE(options.get("max_conflicts") >= 1);

            telemetry::solver_statistics stats;
            stats.inc("conflicts");
            stats.add("propagations", 4u);

            const auto snapshot = stats.snapshot_of();
            REQUIRE(snapshot.conflicts == 1u);
            REQUIRE(snapshot.propagations == 4u);

            telemetry::solver_statistics child_stats;
            child_stats.inc("conflicts");
            child_stats.add("propagations", 2u);

            stats.merge_phase_statistics(child_stats);
            const auto merged = stats.snapshot_of();
            REQUIRE(merged.conflicts == 2u);
            REQUIRE(merged.propagations == 6u);
        }

        SECTION("profile clock tracks phase timing")
        {
            telemetry::profile_clock clock;
            clock.start_phase("search");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            clock.stop_phase("search");

            REQUIRE(clock.current_wall_time() >= 1u);
            REQUIRE(clock.current_process_time() >= 1u);
        }

        SECTION("report formatter renders compact status lines")
        {
            telemetry::report_formatter formatter;
            telemetry::solver_statistics::snapshot snapshot {};
            snapshot.conflicts = 3u;
            snapshot.decisions = 7u;
            snapshot.propagations = 11u;
            snapshot.restarts = 2u;
            snapshot.learned_clauses = 5u;

            const auto stats_line = formatter.format_statistics_line(snapshot);
            REQUIRE(stats_line.find("conflicts=3") != std::string::npos);
            REQUIRE(stats_line.find("decisions=7") != std::string::npos);
            REQUIRE(stats_line.find("propagations=11") != std::string::npos);

            const auto resource_line = formatter.format_resource_line();
            REQUIRE(!resource_line.empty());

            const auto progress_line = formatter.format_progress();
            REQUIRE(!progress_line.empty());
        }

        SECTION("solver facade stores options and resets session state")
        {
            solver option_solver;
            option_solver.set_option("max_conflicts", 11);
            option_solver.set_option("max_decisions", 7);
            option_solver.set_configuration("default");

            const auto initial_stats = option_solver.statistics();
            REQUIRE(initial_stats.conflicts == 0u);
            REQUIRE(initial_stats.decisions == 0u);

            std::vector<literal> option_clause {literal {variable {6u}, false}};
            option_solver.add_clause(std::span<const literal> {option_clause});
            option_solver.solve(solve_request {});

            option_solver.reset_session();
            const auto reset_stats = option_solver.statistics();
            REQUIRE(reset_stats.conflicts == 0u);
            REQUIRE(reset_stats.decisions == 0u);
        }
    }
}