#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <span>
#include <thread>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/c_api_adapter.hpp>
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/solver_state_machine.hpp>
#include <kmx/sat/telemetry/logging_facade.hpp>
#include <kmx/sat/telemetry/profile_clock.hpp>
#include <kmx/sat/telemetry/report_formatter.hpp>
#include <kmx/sat/telemetry/solver_options.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat {

TEST_CASE("solver facade", "[sat]")
{
    using namespace kmx::sat;

    solver sat_solver;
    std::vector<literal> sat_clause {literal {variable {1}, false}};
    sat_solver.add_clause(std::span<const literal> {sat_clause});

    const auto sat_result = sat_solver.solve(solve_request {});
    REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);

    const auto v1 = sat_solver.value_of(variable {1});
    REQUIRE(v1.has_value());
    REQUIRE(*v1);

    solver unsat_solver;
    std::vector<literal> unit_clause {literal {variable {2}, false}};
    unsat_solver.add_clause(std::span<const literal> {unit_clause});
    unsat_solver.assume(literal {variable {2}, true});

    const auto unsat_result = unsat_solver.solve(solve_request {});
    REQUIRE(unsat_result.status_of() == solve_result::status::unsatisfiable);
    REQUIRE(!unsat_result.failed_core().assumptions().empty());
    REQUIRE(unsat_solver.failed(literal {variable {2}, true}));

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

    SECTION("solver state machine tracks lifecycle transitions")
    {
        solver_state_machine machine;
        REQUIRE(machine.current_state() == solver_state_machine::state::configuring);
        REQUIRE(machine.expect_configuring());

        machine.to(solver_state_machine::state::adding);
        REQUIRE(machine.expect_adding());

        machine.to(solver_state_machine::state::solving);
        REQUIRE(machine.expect_solving());

        machine.to(solver_state_machine::state::sat);
        REQUIRE(machine.current_state() == solver_state_machine::state::sat);
    }

    SECTION("solver facade tracks lifecycle state transitions")
    {
        solver lifecycle_solver;
        REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::configuring);

        std::vector<literal> lifecycle_clause {literal {variable {3u}, false}};
        lifecycle_solver.add_clause(std::span<const literal> {lifecycle_clause});
        REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::adding);

        const auto lifecycle_result = lifecycle_solver.solve(solve_request {});
        REQUIRE(lifecycle_result.status_of() == solve_result::status::satisfiable);
        REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::sat);

        lifecycle_solver.reset_session();
        REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::configuring);
    }

    SECTION("solver facade exposes a proof summary through the attached sink")
    {
        proof::tracer::drat concrete_sink;
        proof::tracer::view sink {concrete_sink};
        solver proof_solver;
        proof_solver.attach_proof_sink(sink);

        std::vector<literal> proof_clause {literal {variable {4u}, false}};
        proof_solver.add_clause(std::span<const literal> {proof_clause});
        const auto proof_result = proof_solver.solve(solve_request {});

        const auto proof_summary = proof_result.proof_summary_of();
        REQUIRE(proof_summary.proof_enabled);
        REQUIRE(!proof_summary.proof_checked);
    }

    SECTION("solver facade retains callbacks and reports termination")
    {
        solver callback_solver;
        std::size_t terminate_calls = 0u;
        callback_solver.set_terminate([&]() noexcept {
            ++terminate_calls;
            return true;
        });

        callback_solver.set_learn([&](std::span<const literal>) noexcept {
            ++terminate_calls;
        });

        callback_solver.set_external_propagator([&]() noexcept {
            ++terminate_calls;
        });

        std::vector<literal> callback_clause {literal {variable {5u}, false}};
        callback_solver.add_clause(std::span<const literal> {callback_clause});
        const auto callback_result = callback_solver.solve(solve_request {});

        REQUIRE(callback_result.status_of() == solve_result::status::terminated);
        REQUIRE(terminate_calls == 1u);
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

    SECTION("c api adapter maps IPASIR semantics to the facade")
    {
        c_api_adapter adapter;
        adapter.ipasir_init();
        adapter.ipasir_add(1);
        REQUIRE(adapter.ipasir_solve() == 10);
        REQUIRE(adapter.ipasir_val(1) == 1);

        adapter.ipasir_init();
        adapter.ipasir_add(2);
        adapter.ipasir_assume(-2);
        REQUIRE(adapter.ipasir_solve() == 20);
        REQUIRE(adapter.ipasir_failed(-2));
    }

    // removed std::cout: "solver facade test passed\n";
    }

} // namespace
