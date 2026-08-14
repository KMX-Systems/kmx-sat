#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <kmx/sat/c_api_adapter.hpp>
#include <kmx/sat/ipasir.h>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/solver_state_machine.hpp>
#include <kmx/sat/test_support/statistics_report_assertions.hpp>
#include <kmx/sat/test_support/statistics_report_parser.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("solver facade", "[sat]")
    {
        using namespace kmx::sat;

        solver sat_solver;
        std::vector<literal> sat_clause {literal {variable {1}, false}};
        sat_solver.add_clause(std::span<const literal> {sat_clause});

        const auto sat_result = sat_solver.solve(solve_request {});
        REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);
        REQUIRE(sat_solver.statistics().propagations > 0u);

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

        SECTION("solver facade retains callbacks and reports termination")
        {
            solver callback_solver;
            std::size_t terminate_calls = 0u;
            callback_solver.set_terminate(
                [&]() noexcept
                {
                    ++terminate_calls;
                    return true;
                });

            callback_solver.set_learn([&](std::span<const literal>) noexcept { ++terminate_calls; });

            callback_solver.set_external_propagator([&]() noexcept { ++terminate_calls; });

            std::vector<literal> callback_clause {literal {variable {5u}, false}};
            callback_solver.add_clause(std::span<const literal> {callback_clause});
            const auto callback_result = callback_solver.solve(solve_request {});

            REQUIRE(callback_result.status_of() == solve_result::status::terminated);
            REQUIRE(terminate_calls == 1u);
            REQUIRE(callback_solver.statistics().terminate_callback_calls == 1u);
            REQUIRE(callback_solver.statistics().external_propagator_calls == 0u);
        }

        SECTION("solver facade invokes external and learn callbacks on non-terminated solve")
        {
            solver callback_solver;
            std::size_t external_calls = 0u;
            std::size_t learn_calls = 0u;
            std::size_t learned_literals_seen = 0u;

            callback_solver.set_external_propagator([&]() noexcept { ++external_calls; });
            callback_solver.set_learn(
                [&](const std::span<const literal> learned_clause) noexcept
                {
                    ++learn_calls;
                    learned_literals_seen += learned_clause.size();
                });

            std::vector<literal> callback_clause {literal {variable {7u}, false}};
            callback_solver.add_clause(std::span<const literal> {callback_clause});
            callback_solver.assume(literal {variable {7u}, true});

            const auto callback_result = callback_solver.solve(solve_request {});
            REQUIRE(callback_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(external_calls == 1u);
            REQUIRE(learn_calls >= 1u);
            REQUIRE(learned_literals_seen >= 1u);
            REQUIRE(callback_solver.statistics().learned_clauses == 0u);
            REQUIRE(callback_solver.statistics().external_propagator_calls == 1u);
            REQUIRE(callback_solver.statistics().learn_callback_calls >= 1u);
        }

        SECTION("solver facade releases staged incremental assumptions")
        {
            solver assumption_release_solver;
            std::vector<literal> clause {literal {variable {12u}, false}};
            assumption_release_solver.add_clause(std::span<const literal> {clause});
            assumption_release_solver.assume(literal {variable {12u}, true});
            assumption_release_solver.release_incremental_assumptions();

            const auto result = assumption_release_solver.solve(solve_request {});
            REQUIRE(result.status_of() == solve_result::status::satisfiable);
        }

        SECTION("solver facade maps deterministic limit exits to unknown")
        {
            solver decision_limited_solver;
            std::vector<literal> decision_clause {
                literal {variable {31u}, false},
                literal {variable {32u}, false},
                literal {variable {33u}, false}
            };
            decision_limited_solver.add_clause(std::span<const literal> {decision_clause});

            solve_request decision_limited_request {};
            decision_limited_request.decision_limit = 1u;
            const auto decision_limited_result = decision_limited_solver.solve(decision_limited_request);
            REQUIRE(decision_limited_result.status_of() == solve_result::status::unknown);

            solver conflict_limited_solver;
            conflict_limited_solver.add_clause(std::span<const literal> {std::array<literal, 1> {literal {variable {41u}, false}}});
            conflict_limited_solver.add_clause(std::span<const literal> {std::array<literal, 1> {literal {variable {41u}, true}}});

            solve_request conflict_limited_request {};
            conflict_limited_request.conflict_limit = 1u;
            const auto conflict_limited_result = conflict_limited_solver.solve(conflict_limited_request);
            REQUIRE(conflict_limited_result.status_of() == solve_result::status::unknown);
        }

        SECTION("solver facade applies persisted limit defaults when request limits are unset")
        {
            solver persisted_decision_limited_solver;
            persisted_decision_limited_solver.set_option("decision_limit", 1);
            REQUIRE(persisted_decision_limited_solver.has_persisted_configuration());
            std::vector<literal> decision_clause {
                literal {variable {51u}, false},
                literal {variable {52u}, false},
                literal {variable {53u}, false}
            };
            persisted_decision_limited_solver.add_clause(std::span<const literal> {decision_clause});

            const auto persisted_decision_result = persisted_decision_limited_solver.solve(solve_request {});
            REQUIRE(persisted_decision_result.status_of() == solve_result::status::unknown);

            solver persisted_conflict_limited_solver;
            persisted_conflict_limited_solver.set_option("conflict_limit", 1);
            REQUIRE(persisted_conflict_limited_solver.has_persisted_configuration());
            persisted_conflict_limited_solver.add_clause(
                std::span<const literal> {std::array<literal, 1> {literal {variable {61u}, false}}});
            persisted_conflict_limited_solver.add_clause(
                std::span<const literal> {std::array<literal, 1> {literal {variable {61u}, true}}});

            const auto persisted_conflict_result = persisted_conflict_limited_solver.solve(solve_request {});
            REQUIRE(persisted_conflict_result.status_of() == solve_result::status::unknown);
        }

        SECTION("solver facade stores options/configuration and preserves persistence marker across reset")
        {
            solver option_solver;
            REQUIRE_FALSE(option_solver.persisted_option_subset());
            REQUIRE_FALSE(option_solver.has_persisted_configuration());
            REQUIRE_FALSE(option_solver.configured_conflict_limit().has_value());
            REQUIRE_FALSE(option_solver.configured_decision_limit().has_value());
            REQUIRE_FALSE(option_solver.configured_enabled_pass_mask().has_value());
            REQUIRE_FALSE(option_solver.configured_strict_mode().has_value());
            REQUIRE(option_solver.configuration_profile_name().empty());
            REQUIRE(option_solver.decision_maintenance_intervals()[0] == 16u);
            REQUIRE(option_solver.decision_maintenance_intervals()[1] == 8u);
            REQUIRE(option_solver.decision_maintenance_intervals()[2] == 4u);
            REQUIRE_FALSE(option_solver.chb_enabled());
            REQUIRE(option_solver.reduction_fraction_percent() == 50u);
            REQUIRE(option_solver.activity_retention_threshold() == 2.0);
            REQUIRE(option_solver.glue_restart_threshold_percent() == 0u);
            REQUIRE_FALSE(option_solver.cold_storage_enabled());
            REQUIRE(option_solver.cold_footprint_bytes() == 0u);

            option_solver.set_option("conflict_limit", 3);
            REQUIRE(option_solver.persisted_option_subset());
            REQUIRE(option_solver.has_persisted_configuration());
            REQUIRE(option_solver.configured_conflict_limit().has_value());
            REQUIRE(option_solver.configured_conflict_limit().value() == 3u);
            REQUIRE(option_solver.statistics().option_updates == 1u);

            option_solver.set_option("decision_limit", 17);
            REQUIRE(option_solver.configured_decision_limit().has_value());
            REQUIRE(option_solver.configured_decision_limit().value() == 17u);
            REQUIRE(option_solver.statistics().option_updates == 2u);

            option_solver.set_option("decision_conflict_maintenance_interval", 2);
            option_solver.set_option("decision_chb_decay_interval", 3);
            option_solver.set_option("decision_restart_decay_interval", 5);
            option_solver.set_option("chb_enabled", 1);
            option_solver.set_option("reduction_fraction_percent", 25);
            option_solver.set_option("glue_restart_threshold_percent", 110);
            option_solver.set_option("cold_storage_enabled", 1);
            option_solver.set_option("activity_retention_threshold_percent", 250);
            REQUIRE(option_solver.statistics().option_updates == 10u);
            REQUIRE(option_solver.decision_maintenance_intervals()[0] == 2u);
            REQUIRE(option_solver.decision_maintenance_intervals()[1] == 3u);
            REQUIRE(option_solver.decision_maintenance_intervals()[2] == 5u);
            REQUIRE(option_solver.chb_enabled());
            REQUIRE(option_solver.reduction_fraction_percent() == 25u);
            REQUIRE(option_solver.glue_restart_threshold_percent() == 110u);
            REQUIRE(option_solver.cold_storage_enabled());
            REQUIRE(option_solver.activity_retention_threshold() == 2.5);

            option_solver.set_option("unknown_option", 9);
            REQUIRE(option_solver.configured_decision_limit().value() == 17u);
            REQUIRE(option_solver.statistics().option_updates == 10u);

            option_solver.set_configuration("safe");
            REQUIRE(option_solver.persisted_option_subset());
            REQUIRE(option_solver.has_persisted_configuration());
            REQUIRE(option_solver.configuration_profile_name() == "safe");
            REQUIRE(option_solver.configured_strict_mode().has_value());
            REQUIRE(option_solver.configured_strict_mode().value());
            REQUIRE(option_solver.configured_enabled_pass_mask().has_value());
            REQUIRE(option_solver.configured_enabled_pass_mask().value() == 0u);
            REQUIRE(option_solver.statistics().configuration_updates == 1u);

            option_solver.set_configuration("balanced");
            REQUIRE(option_solver.configuration_profile_name() == "balanced");
            REQUIRE(option_solver.configured_strict_mode().has_value());
            REQUIRE_FALSE(option_solver.configured_strict_mode().value());
            REQUIRE(option_solver.configured_enabled_pass_mask().has_value());
            REQUIRE(option_solver.configured_enabled_pass_mask().value() == ~0ull);
            REQUIRE(option_solver.statistics().configuration_updates == 2u);

            option_solver.set_configuration("bounded");
            REQUIRE(option_solver.configuration_profile_name() == "bounded");
            REQUIRE(option_solver.configured_conflict_limit().has_value());
            REQUIRE(option_solver.configured_conflict_limit().value() == 1000u);
            REQUIRE(option_solver.configured_decision_limit().has_value());
            REQUIRE(option_solver.configured_decision_limit().value() == 10000u);
            REQUIRE(option_solver.statistics().configuration_updates == 3u);

            option_solver.set_configuration("aggressive");
            REQUIRE(option_solver.configuration_profile_name() == "aggressive");
            REQUIRE(option_solver.configured_conflict_limit().has_value());
            REQUIRE(option_solver.configured_conflict_limit().value() == 0u);
            REQUIRE(option_solver.configured_decision_limit().has_value());
            REQUIRE(option_solver.configured_decision_limit().value() == 0u);
            REQUIRE(option_solver.configured_strict_mode().has_value());
            REQUIRE_FALSE(option_solver.configured_strict_mode().value());
            REQUIRE(option_solver.statistics().configuration_updates == 4u);

            option_solver.set_configuration("unknown-profile");
            REQUIRE(option_solver.configuration_profile_name().empty());
            REQUIRE_FALSE(option_solver.has_persisted_configuration());
            REQUIRE(option_solver.statistics().configuration_updates == 4u);

            option_solver.set_option("strict_mode", 1);
            REQUIRE(option_solver.has_persisted_configuration());
            REQUIRE(option_solver.configured_strict_mode().has_value());
            REQUIRE(option_solver.configured_strict_mode().value());

            option_solver.clear_persisted_configuration();
            REQUIRE_FALSE(option_solver.has_persisted_configuration());
            REQUIRE_FALSE(option_solver.configured_conflict_limit().has_value());
            REQUIRE_FALSE(option_solver.configured_decision_limit().has_value());
            REQUIRE_FALSE(option_solver.configured_enabled_pass_mask().has_value());
            REQUIRE_FALSE(option_solver.configured_strict_mode().has_value());
            REQUIRE(option_solver.configuration_profile_name().empty());

            option_solver.reset_session();
            REQUIRE_FALSE(option_solver.has_persisted_configuration());

            option_solver.set_option("conflict_limit", 3);
            REQUIRE(option_solver.has_persisted_configuration());
            option_solver.reset_session();
            REQUIRE(option_solver.persisted_option_subset());
            REQUIRE(option_solver.has_persisted_configuration());
            // The prior maintenance-interval overrides (2/3/5) were wiped by clear_persisted_configuration() above;
            // only conflict_limit was re-persisted since, so reset_session() correctly reapplies the defaults here.
            REQUIRE(option_solver.decision_maintenance_intervals()[0] == 16u);
            REQUIRE(option_solver.decision_maintenance_intervals()[1] == 8u);
            REQUIRE(option_solver.decision_maintenance_intervals()[2] == 4u);
            REQUIRE_FALSE(option_solver.cold_storage_enabled());

            std::vector<literal> clause {literal {variable {9u}, false}};
            option_solver.add_clause(std::span<const literal> {clause});
            const auto result = option_solver.solve(solve_request {});
            REQUIRE(result.status_of() == solve_result::status::satisfiable);
            REQUIRE(option_solver.proof_buffered_event_count() >= 1u);
        }

        SECTION("solver facade statistics snapshots are monotonic before reset")
        {
            solver monotonic_solver;
            const auto baseline = monotonic_solver.statistics();

            monotonic_solver.set_option("conflict_limit", 5);
            const auto after_option = monotonic_solver.statistics();
            REQUIRE(telemetry::solver_statistics::snapshot_monotonic(baseline, after_option));

            monotonic_solver.set_configuration("bounded");
            const auto after_configuration = monotonic_solver.statistics();
            REQUIRE(telemetry::solver_statistics::snapshot_monotonic(after_option, after_configuration));

            std::size_t external_calls = 0u;
            std::size_t learn_calls = 0u;
            monotonic_solver.set_terminate([]() noexcept { return false; });
            monotonic_solver.set_external_propagator([&]() noexcept { ++external_calls; });
            monotonic_solver.set_learn([&](std::span<const literal>) noexcept { ++learn_calls; });
            std::vector<literal> clause {literal {variable {19u}, false}};
            monotonic_solver.add_clause(std::span<const literal> {clause});
            monotonic_solver.assume(literal {variable {19u}, true});

            const auto result = monotonic_solver.solve(solve_request {});
            REQUIRE(result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(external_calls == 1u);
            REQUIRE(learn_calls >= 1u);

            const auto after_solve = monotonic_solver.statistics();
            REQUIRE(telemetry::solver_statistics::snapshot_monotonic(after_configuration, after_solve));
            REQUIRE(after_solve.terminate_callback_calls >= 1u);
            REQUIRE(after_solve.external_propagator_calls == 1u);
            REQUIRE(after_solve.learn_callback_calls >= 1u);

            monotonic_solver.reset_session();
            const auto after_reset = monotonic_solver.statistics();
            REQUIRE(after_reset.conflicts == 0u);
            REQUIRE(after_reset.decisions == 0u);
            REQUIRE(after_reset.propagations == 0u);
            REQUIRE(after_reset.restarts == 0u);
            REQUIRE(after_reset.learned_clauses == 0u);
            REQUIRE(after_reset.terminate_callback_calls == 0u);
            REQUIRE(after_reset.learn_callback_calls == 0u);
            REQUIRE(after_reset.external_propagator_calls == 0u);
            REQUIRE(after_reset.option_updates == 0u);
            REQUIRE(after_reset.configuration_updates == 0u);
        }

        SECTION("solver facade formats statistics lines in compact and verbose modes")
        {
            solver reporting_solver;
            REQUIRE(reporting_solver.statistics_report_emission_count() == 0u);
            REQUIRE(reporting_solver.last_emitted_statistics_report_line().empty());
            REQUIRE_FALSE(reporting_solver.last_emitted_statistics_snapshot().has_value());
            REQUIRE_FALSE(reporting_solver.previous_emitted_statistics_snapshot().has_value());
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_monotonic());
            std::vector<literal> clause {literal {variable {23u}, false}};
            reporting_solver.add_clause(std::span<const literal> {clause});
            reporting_solver.assume(literal {variable {23u}, true});

            const auto report_solve_result = reporting_solver.solve(solve_request {});
            REQUIRE(report_solve_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(reporting_solver.statistics_report_emission_count() == 2u);
            REQUIRE(!reporting_solver.last_emitted_statistics_report_line().empty());
            REQUIRE(reporting_solver.last_emitted_statistics_snapshot().has_value());
            REQUIRE(reporting_solver.previous_emitted_statistics_snapshot().has_value());
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_monotonic());
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_delta().has_value());
            REQUIRE(telemetry::solver_statistics::snapshot_monotonic(
                reporting_solver.previous_emitted_statistics_snapshot().value(),
                reporting_solver.last_emitted_statistics_snapshot().value()));
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_delta().value().conflicts == 1u);
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_delta().value().decisions == 0u);
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_delta().value().option_updates == 0u);
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_delta().value().configuration_updates == 0u);

            const auto compact_previous_line = reporting_solver.previous_emitted_statistics_report_line();
            const auto compact_latest_line = reporting_solver.last_emitted_statistics_report_line();
            const auto compact_previous_snapshot = reporting_solver.previous_emitted_statistics_snapshot().value();
            const auto compact_tail_delta = reporting_solver.emitted_statistics_snapshot_tail_delta().value();
            const auto compact_previous_parsed = test_support::parse_statistics_report_line(compact_previous_line);
            const auto compact_latest_parsed = test_support::parse_statistics_report_line(compact_latest_line);
            REQUIRE(test_support::compact_statistics_report_exactly_matches_snapshot(compact_previous_parsed, compact_previous_snapshot));
            REQUIRE(test_support::compact_statistics_report_delta_matches_snapshot_delta(
                compact_previous_parsed,
                compact_latest_parsed,
                compact_tail_delta));

            reporting_solver.set_statistics_report_detail(solver::statistics_report_detail::verbose);
            reporting_solver.assume(literal {variable {23u}, true});
            const auto verbose_tail_result = reporting_solver.solve(solve_request {});
            REQUIRE(verbose_tail_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(reporting_solver.statistics_report_emission_count() == 4u);

            const auto verbose_latest_line = reporting_solver.last_emitted_statistics_report_line();
            const auto verbose_previous_line = reporting_solver.previous_emitted_statistics_report_line();
            const auto verbose_latest_snapshot = reporting_solver.last_emitted_statistics_snapshot().value();
            const auto verbose_tail_delta = reporting_solver.emitted_statistics_snapshot_tail_delta().value();
            const auto verbose_latest_parsed = test_support::parse_statistics_report_line(verbose_latest_line);
            const auto verbose_previous_parsed = test_support::parse_statistics_report_line(verbose_previous_line);
            REQUIRE(test_support::verbose_statistics_report_exactly_matches_snapshot(verbose_latest_parsed, verbose_latest_snapshot));
            REQUIRE(test_support::verbose_statistics_report_delta_matches_snapshot_delta(
                verbose_previous_parsed,
                verbose_latest_parsed,
                verbose_tail_delta));

            reporting_solver.set_statistics_report_detail(solver::statistics_report_detail::compact);
            const auto compact_line = reporting_solver.statistics_report_line();
            REQUIRE(compact_line.find("conflicts=") != std::string::npos);
            REQUIRE(compact_line.find("option_updates=") == std::string::npos);

            reporting_solver.set_statistics_report_detail(solver::statistics_report_detail::verbose);
            REQUIRE(reporting_solver.statistics_report_detail_of() == solver::statistics_report_detail::verbose);
            const auto verbose_line = reporting_solver.statistics_report_line();
            REQUIRE(verbose_line.find("option_updates=") != std::string::npos);
            REQUIRE(verbose_line.find("configuration_updates=") != std::string::npos);

            reporting_solver.set_option("statistics_verbose_reporting", 0);
            REQUIRE(reporting_solver.statistics_report_detail_of() == solver::statistics_report_detail::compact);
            reporting_solver.set_option("statistics_verbose_reporting", 1);
            REQUIRE(reporting_solver.statistics_report_detail_of() == solver::statistics_report_detail::verbose);

            reporting_solver.reset_session();
            REQUIRE(reporting_solver.statistics_report_emission_count() == 0u);
            REQUIRE(reporting_solver.last_emitted_statistics_report_line().empty());
            REQUIRE_FALSE(reporting_solver.last_emitted_statistics_snapshot().has_value());
            REQUIRE_FALSE(reporting_solver.previous_emitted_statistics_snapshot().has_value());
            REQUIRE(reporting_solver.emitted_statistics_snapshot_tail_monotonic());
            REQUIRE_FALSE(reporting_solver.emitted_statistics_snapshot_tail_delta().has_value());
        }

        SECTION("c api adapter maps IPASIR semantics to the facade")
        {
            c_api_adapter adapter;
            REQUIRE_FALSE(adapter.ipasir_persisted_option_subset());
            REQUIRE_FALSE(adapter.ipasir_has_persisted_configuration());
            REQUIRE(adapter.ipasir_configured_conflict_limit() == -1);
            REQUIRE(adapter.ipasir_configured_decision_limit() == -1);
            REQUIRE(adapter.ipasir_configured_strict_mode() == -1);
            REQUIRE(adapter.ipasir_configuration_profile_name().empty());
            REQUIRE_FALSE(adapter.ipasir_statistics_emission_tail_delta().has_value());

            adapter.ipasir_set_option("conflict_limit", 5);
            REQUIRE(adapter.ipasir_persisted_option_subset());
            REQUIRE(adapter.ipasir_has_persisted_configuration());
            REQUIRE(adapter.ipasir_configured_conflict_limit() == 5);

            adapter.ipasir_set_option("decision_limit", 11);
            REQUIRE(adapter.ipasir_configured_decision_limit() == 11);

            adapter.ipasir_set_configuration("safe");
            REQUIRE(adapter.ipasir_has_persisted_configuration());
            REQUIRE(adapter.ipasir_configuration_profile_name() == "safe");
            REQUIRE(adapter.ipasir_configured_strict_mode() == 1);

            adapter.ipasir_set_configuration("bounded");
            REQUIRE(adapter.ipasir_configuration_profile_name() == "bounded");
            REQUIRE(adapter.ipasir_configured_conflict_limit() == 1000);
            REQUIRE(adapter.ipasir_configured_decision_limit() == 10000);

            adapter.ipasir_set_configuration("unknown");
            REQUIRE_FALSE(adapter.ipasir_has_persisted_configuration());
            REQUIRE(adapter.ipasir_configuration_profile_name().empty());
            REQUIRE(adapter.ipasir_configured_conflict_limit() == -1);

            adapter.ipasir_init();
            adapter.ipasir_add(1);
            REQUIRE(adapter.ipasir_solve() == 10);
            REQUIRE(adapter.ipasir_val(1) == 1);
            REQUIRE(adapter.ipasir_statistics_emission_tail_delta().has_value());

            adapter.ipasir_init();
            REQUIRE_FALSE(adapter.ipasir_statistics_emission_tail_delta().has_value());
            adapter.ipasir_add(2);
            adapter.ipasir_assume(-2);
            REQUIRE(adapter.ipasir_solve() == 20);
            REQUIRE(adapter.ipasir_failed(-2));
            REQUIRE(adapter.ipasir_statistics_emission_tail_delta().has_value());

            adapter.ipasir_set_option("strict_mode", 1);
            REQUIRE(adapter.ipasir_has_persisted_configuration());
            REQUIRE(adapter.ipasir_configured_strict_mode() == 1);
            adapter.ipasir_clear_persisted_configuration();
            REQUIRE_FALSE(adapter.ipasir_has_persisted_configuration());
            REQUIRE(adapter.ipasir_configured_strict_mode() == -1);
            REQUIRE(adapter.ipasir_configuration_profile_name().empty());

            adapter.ipasir_set_statistics_verbose_reporting(0);
            REQUIRE(adapter.ipasir_statistics_verbose_reporting() == 0);
            const auto baseline_report_emissions = adapter.ipasir_statistics_report_emission_count();
            REQUIRE(!adapter.ipasir_last_emitted_statistics_report_line().empty());
            REQUIRE(adapter.ipasir_statistics_emission_tail_monotonic() == 1);
            const auto compact_before_report_line = adapter.ipasir_statistics_report_line();
            adapter.ipasir_add(4);
            adapter.ipasir_assume(-4);
            REQUIRE(adapter.ipasir_solve() == 20);
            REQUIRE(adapter.ipasir_statistics_report_emission_count() == baseline_report_emissions + 2u);
            REQUIRE(!adapter.ipasir_last_emitted_statistics_report_line().empty());
            REQUIRE(!adapter.ipasir_previous_emitted_statistics_report_line().empty());
            REQUIRE(adapter.ipasir_statistics_emission_tail_monotonic() == 1);
            REQUIRE(adapter.ipasir_statistics_emission_tail_delta().has_value());
            REQUIRE(adapter.ipasir_last_emitted_statistics_snapshot().has_value());
            REQUIRE(adapter.ipasir_previous_emitted_statistics_snapshot().has_value());
            REQUIRE(adapter.ipasir_statistics_emission_tail_delta().value().conflicts == 1u);
            REQUIRE(adapter.ipasir_statistics_emission_tail_delta().value().decisions == 0u);
            const auto compact_after_report_line = adapter.ipasir_last_emitted_statistics_report_line();
            const auto compact_previous_report_line = adapter.ipasir_previous_emitted_statistics_report_line();
            const auto compact_before_report_parsed = test_support::parse_statistics_report_line(compact_before_report_line);
            const auto compact_previous_report_parsed = test_support::parse_statistics_report_line(compact_previous_report_line);
            const auto compact_after_report_parsed = test_support::parse_statistics_report_line(compact_after_report_line);
            REQUIRE(test_support::compact_statistics_report_has_compact_schema(compact_before_report_parsed));
            REQUIRE(test_support::compact_statistics_report_exactly_matches_snapshot(
                compact_previous_report_parsed,
                adapter.ipasir_previous_emitted_statistics_snapshot().value()));
            REQUIRE(test_support::compact_statistics_report_has_compact_schema(compact_after_report_parsed));
            REQUIRE(test_support::compact_statistics_report_exactly_matches_snapshot(
                compact_after_report_parsed,
                adapter.ipasir_last_emitted_statistics_snapshot().value()));
            REQUIRE(test_support::compact_statistics_report_delta_matches_snapshot_delta(
                compact_previous_report_parsed,
                compact_after_report_parsed,
                adapter.ipasir_statistics_emission_tail_delta().value()));

            adapter.ipasir_set_statistics_verbose_reporting(1);
            REQUIRE(adapter.ipasir_statistics_verbose_reporting() == 1);
            const auto verbose_before_report_line = adapter.ipasir_statistics_report_line();
            adapter.ipasir_add(6);
            adapter.ipasir_assume(-6);
            REQUIRE(adapter.ipasir_solve() == 20);
            REQUIRE(adapter.ipasir_statistics_emission_tail_monotonic() == 1);
            REQUIRE(adapter.ipasir_statistics_emission_tail_delta().has_value());
            REQUIRE(adapter.ipasir_last_emitted_statistics_snapshot().has_value());
            REQUIRE(adapter.ipasir_previous_emitted_statistics_snapshot().has_value());
            const auto verbose_after_report_line = adapter.ipasir_last_emitted_statistics_report_line();
            adapter.ipasir_set_statistics_verbose_reporting(0);
            const auto compact_verbose_after_report_line = adapter.ipasir_statistics_report_line();
            adapter.ipasir_set_statistics_verbose_reporting(1);
            const auto verbose_previous_report_line = adapter.ipasir_previous_emitted_statistics_report_line();
            const auto verbose_before_report_parsed = test_support::parse_statistics_report_line(verbose_before_report_line);
            const auto verbose_previous_report_parsed = test_support::parse_statistics_report_line(verbose_previous_report_line);
            const auto verbose_after_report_parsed = test_support::parse_statistics_report_line(verbose_after_report_line);
            const auto compact_verbose_after_report_parsed =
                test_support::parse_statistics_report_line(compact_verbose_after_report_line);
            REQUIRE(test_support::verbose_statistics_report_has_verbose_schema(verbose_before_report_parsed));
            REQUIRE(test_support::verbose_statistics_report_exactly_matches_snapshot(
                verbose_previous_report_parsed,
                adapter.ipasir_previous_emitted_statistics_snapshot().value()));
            REQUIRE(test_support::verbose_statistics_report_has_verbose_schema(verbose_after_report_parsed));
            REQUIRE(test_support::verbose_statistics_report_exactly_matches_snapshot(
                verbose_after_report_parsed,
                adapter.ipasir_last_emitted_statistics_snapshot().value()));
            REQUIRE(test_support::verbose_statistics_report_extends_compact_consistently(
                compact_verbose_after_report_parsed,
                verbose_after_report_parsed));
            REQUIRE(test_support::verbose_statistics_report_delta_matches_snapshot_delta(
                verbose_previous_report_parsed,
                verbose_after_report_parsed,
                adapter.ipasir_statistics_emission_tail_delta().value()));

            adapter.ipasir_init();
            adapter.ipasir_add(1);
            adapter.ipasir_add(0);
            adapter.ipasir_add(-1);
            adapter.ipasir_add(0);
            REQUIRE(adapter.ipasir_solve() == 20);
            REQUIRE_FALSE(adapter.ipasir_failed(1));

            adapter.ipasir_init();
            adapter.ipasir_add(7);
            adapter.ipasir_add(0);
            REQUIRE(adapter.ipasir_solve() == 10);
            REQUIRE(adapter.ipasir_val(7) == 7);
            REQUIRE(adapter.ipasir_val(-7) == 7);
            REQUIRE(adapter.ipasir_val(0) == 0);
        }

        SECTION("public IPASIR C ABI handles edge cases")
        {
            auto* c_solver = ipasir_init();
            REQUIRE(c_solver != nullptr);
            ipasir_add(c_solver, 1);
            ipasir_add(c_solver, 1);
            ipasir_add(c_solver, -1);
            ipasir_add(c_solver, 1);
            ipasir_add(c_solver, 0);
            ipasir_add(c_solver, 1);
            ipasir_add(c_solver, 0);
            ipasir_add(c_solver, -1);
            ipasir_add(c_solver, 0);
            REQUIRE(ipasir_solve(c_solver) == 20);
            REQUIRE(ipasir_failed(c_solver, 1) == 0);
            REQUIRE(ipasir_val(c_solver, 0) == 0);
            ipasir_assume(c_solver, 1);
            REQUIRE(ipasir_solve(c_solver) == 20);
            ipasir_release(c_solver);
            REQUIRE(ipasir_solve(nullptr) == 0);
            REQUIRE(ipasir_val(nullptr, 1) == 0);
            REQUIRE(ipasir_failed(nullptr, 1) == 0);
        }

        // removed std::cout: "solver facade test passed\n";
    }

    TEST_CASE("solver facade drives deterministic restart and reduction schedules", "[sat]")
    {
        solver configured_solver;
        configured_solver.set_option("restart_interval", 1);
        configured_solver.set_option("decision_restart_interval", 1);
        configured_solver.set_option("reduction_interval", 1);
        configured_solver.set_option("reduction_fraction_percent", 100);

        const std::array<std::array<literal, 3>, 8> clauses {
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, true}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, true}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, false}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, true}, literal {variable {3u}, false}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, true}, literal {variable {3u}, false}},
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, false}, literal {variable {3u}, false}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, false}},
        };
        for (const auto& clause: clauses)
        {
            configured_solver.add_clause(std::span<const literal> {clause});
        }

        const auto result = configured_solver.solve(solve_request {});
        const auto statistics = configured_solver.statistics();

        REQUIRE(result.status_of() == solve_result::status::unsatisfiable);
        REQUIRE(statistics.conflicts > 0u);
        REQUIRE(statistics.learned_clauses > 0u);
        REQUIRE(statistics.restarts > 0u);
        REQUIRE(statistics.reduction_passes > 0u);
    }

    TEST_CASE("public IPASIR compatibility matrix", "[sat]")
    {
        REQUIRE(ipasir_solve(nullptr) == 0);
        ipasir_add(nullptr, 1);
        ipasir_assume(nullptr, 1);
        REQUIRE(ipasir_val(nullptr, 1) == 0);
        REQUIRE(ipasir_failed(nullptr, 1) == 0);
        ipasir_release(nullptr);

        auto* empty_solver = ipasir_init();
        REQUIRE(empty_solver != nullptr);
        REQUIRE(ipasir_solve(empty_solver) == 10);
        REQUIRE(ipasir_val(empty_solver, 1) == 0);
        ipasir_release(empty_solver);

        auto* empty_clause_solver = ipasir_init();
        ipasir_add(empty_clause_solver, 0);
        REQUIRE(ipasir_solve(empty_clause_solver) == 20);
        ipasir_release(empty_clause_solver);

        auto* tautology_solver = ipasir_init();
        ipasir_add(tautology_solver, 1);
        ipasir_add(tautology_solver, 1);
        ipasir_add(tautology_solver, -1);
        ipasir_add(tautology_solver, 0);
        REQUIRE(ipasir_solve(tautology_solver) == 10);
        ipasir_release(tautology_solver);

        auto* assumption_solver = ipasir_init();
        ipasir_add(assumption_solver, 1);
        ipasir_add(assumption_solver, 0);
        ipasir_assume(assumption_solver, -1);
        REQUIRE(ipasir_solve(assumption_solver) == 20);
        REQUIRE(ipasir_failed(assumption_solver, -1) == 1);
        REQUIRE(ipasir_failed(assumption_solver, 1) == 0);
        ipasir_release(assumption_solver);

        auto* invalid_literal_solver = ipasir_init();
        constexpr auto invalid_literal = std::numeric_limits<std::int32_t>::min();
        ipasir_add(invalid_literal_solver, invalid_literal);
        ipasir_assume(invalid_literal_solver, invalid_literal);
        REQUIRE(ipasir_val(invalid_literal_solver, invalid_literal) == 0);
        REQUIRE(ipasir_failed(invalid_literal_solver, invalid_literal) == 0);
        REQUIRE(ipasir_solve(invalid_literal_solver) == 10);
        ipasir_release(invalid_literal_solver);
    }

} // namespace
