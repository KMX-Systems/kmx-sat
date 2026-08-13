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
#include <kmx/sat/test_support/statistics_report_assertions.hpp>
#include <kmx/sat/test_support/statistics_report_parser.hpp>
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
            stats.inc("terminate_callback_calls");
            stats.add("learn_callback_calls", 2u);
            stats.inc("external_propagator_calls");
            stats.add("option_updates", 3u);
            stats.inc("configuration_updates");

            const auto snapshot = stats.snapshot_of();
            REQUIRE(snapshot.conflicts == 1u);
            REQUIRE(snapshot.propagations == 4u);
            REQUIRE(snapshot.terminate_callback_calls == 1u);
            REQUIRE(snapshot.learn_callback_calls == 2u);
            REQUIRE(snapshot.external_propagator_calls == 1u);
            REQUIRE(snapshot.option_updates == 3u);
            REQUIRE(snapshot.configuration_updates == 1u);

            telemetry::solver_statistics child_stats;
            child_stats.inc("conflicts");
            child_stats.add("propagations", 2u);
            child_stats.inc("terminate_callback_calls");
            child_stats.inc("configuration_updates");

            stats.merge_phase_statistics(child_stats);
            const auto merged = stats.snapshot_of();
            REQUIRE(merged.conflicts == 2u);
            REQUIRE(merged.propagations == 6u);
            REQUIRE(merged.terminate_callback_calls == 2u);
            REQUIRE(merged.configuration_updates == 2u);

            const auto delta = telemetry::solver_statistics::snapshot_delta_between(snapshot, merged);
            REQUIRE(delta.conflicts == 1u);
            REQUIRE(delta.propagations == 2u);
            REQUIRE(delta.terminate_callback_calls == 1u);
            REQUIRE(delta.configuration_updates == 1u);
            REQUIRE(delta.learn_callback_calls == 0u);

            const auto saturating_delta = telemetry::solver_statistics::snapshot_delta_between(merged, snapshot);
            REQUIRE(saturating_delta.conflicts == 0u);
            REQUIRE(saturating_delta.propagations == 0u);
            REQUIRE(saturating_delta.terminate_callback_calls == 0u);
            REQUIRE(saturating_delta.configuration_updates == 0u);

            stats.reset_epoch_counters();
            const auto reset = stats.snapshot_of();
            REQUIRE(reset.conflicts == 0u);
            REQUIRE(reset.propagations == 0u);
            REQUIRE(reset.terminate_callback_calls == 0u);
            REQUIRE(reset.learn_callback_calls == 0u);
            REQUIRE(reset.external_propagator_calls == 0u);
            REQUIRE(reset.option_updates == 0u);
            REQUIRE(reset.configuration_updates == 0u);
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
            snapshot.terminate_callback_calls = 13u;
            snapshot.learn_callback_calls = 17u;
            snapshot.external_propagator_calls = 19u;
            snapshot.option_updates = 23u;
            snapshot.configuration_updates = 29u;

            const auto stats_line = formatter.format_statistics_line(snapshot);
            const auto verbose_stats_line = formatter.format_statistics_line_verbose(snapshot);
            REQUIRE(stats_line.find("conflicts=3") != std::string::npos);
            REQUIRE(stats_line.find("decisions=7") != std::string::npos);
            REQUIRE(stats_line.find("propagations=11") != std::string::npos);
            REQUIRE(verbose_stats_line.find("option_updates=23") != std::string::npos);
            REQUIRE(verbose_stats_line.find("configuration_updates=29") != std::string::npos);

            const auto compact_parsed = test_support::parse_statistics_report_line(stats_line);
            REQUIRE(test_support::compact_statistics_report_exactly_matches_snapshot(compact_parsed, snapshot));

            const auto verbose_parsed = test_support::parse_statistics_report_line(verbose_stats_line);
            REQUIRE(test_support::verbose_statistics_report_exactly_matches_snapshot(verbose_parsed, snapshot));

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

            const auto after_configuration = option_solver.statistics();
            REQUIRE(after_configuration.option_updates == 0u);
            REQUIRE(after_configuration.configuration_updates == 0u);

            option_solver.set_option("conflict_limit", 11);
            option_solver.set_option("decision_limit", 7);
            option_solver.set_configuration("bounded");

            const auto initial_stats = option_solver.statistics();
            REQUIRE(initial_stats.conflicts == 0u);
            REQUIRE(initial_stats.decisions == 0u);
            REQUIRE(initial_stats.option_updates == 2u);
            REQUIRE(initial_stats.configuration_updates == 1u);

            std::vector<literal> option_clause {literal {variable {6u}, false}};
            option_solver.add_clause(std::span<const literal> {option_clause});
            option_solver.solve(solve_request {});

            option_solver.reset_session();
            const auto reset_stats = option_solver.statistics();
            REQUIRE(reset_stats.conflicts == 0u);
            REQUIRE(reset_stats.decisions == 0u);
            REQUIRE(reset_stats.option_updates == 0u);
            REQUIRE(reset_stats.configuration_updates == 0u);
        }
    }
}