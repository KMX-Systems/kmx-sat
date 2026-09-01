#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <vector>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>
#include <kmx/sat/simplify/eliminator/variable/fast.hpp>
#include <kmx/sat/simplify/factorizer.hpp>
#include <kmx/sat/simplify/scheduler/inprocess.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{

    TEST_CASE("inprocess simplify", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);
        scheduler.set_conflicts_seen(64u);
        scheduler.set_restart_count(2u);
        REQUIRE(scheduler.should_run());
        REQUIRE(scheduler.compute_budget() >= 1u);
        scheduler.run_epoch();
        scheduler.report_epoch_summary();
        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_budget() > 0u);
        REQUIRE(scheduler.enabled_pass_count() == 3u);
        REQUIRE(scheduler.last_passes_executed() >= 1u);
        REQUIRE(scheduler.reported_summary_count() == 1u);
        REQUIRE(std::all_of(scheduler.last_reported_summaries().begin(), scheduler.last_reported_summaries().end(),
                            [](const scheduler::inprocess::pass_summary& summary) noexcept
                            { return summary.executed || summary.skipped_by_memory_policy || summary.skipped_by_proof_format; }));

        // Periodic trigger windows prevent rerunning inprocess without new progress.
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 1u);

        scheduler.set_conflicts_seen(96u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 2u);

        // Counter reset between solve calls restarts trigger windows.
        scheduler.set_conflicts_seen(0u);
        scheduler.set_restart_count(0u);
        scheduler.set_restart_count(scheduler.restart_trigger_window());
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 3u);

        // Bounded elimination is database-backed: resolving (1 v 2) with (-1 v 3) yields the single resolvent
        // (2 v 3), so eliminating variable 1 replaces two clauses with one.
        cdcl::clause::database bounded_database;
        cdcl::stack::extension bounded_extension;
        const std::array<literal, 2> bounded_first {literal {variable {1u}, false}, literal {variable {2u}, false}};
        const std::array<literal, 2> bounded_second {literal {variable {1u}, true}, literal {variable {3u}, false}};
        bounded_database.add_clause(std::span<const literal> {bounded_first}, false);
        bounded_database.add_clause(std::span<const literal> {bounded_second}, false);

        eliminator::variable::bounded bounded;
        bounded.attach_clause_database(bounded_database);
        bounded.attach_extension_stack(bounded_extension);
        bounded.refresh_occurrence_index();
        REQUIRE(bounded.score_variable(variable {1u}) <= 0);
        bounded.run();
        REQUIRE(bounded.elimination_count() >= 1u);
        REQUIRE(!bounded.eliminated_variables().empty());
        REQUIRE(bounded_extension.size() >= 1u);

        std::vector<std::vector<literal>> bounded_clauses {{literal {variable {1u}, false}, literal {variable {2u}, false}},
                                                           {literal {variable {1u}, true}, literal {variable {3u}, false}}};
        eliminator::variable::fast fast;
        fast.set_clauses(bounded_clauses);
        REQUIRE(fast.cheap_can_eliminate(variable {1u}));
        fast.run_fast_round();
        REQUIRE(fast.fast_round_count() == 1u);
        REQUIRE(fast.elimination_count() == 1u);

        factorizer factorizer;
        std::vector<std::vector<literal>> factor_clauses {{literal {variable {4u}, false}, literal {variable {5u}, false}},
                                                          {literal {variable {4u}, false}, literal {variable {6u}, true}}};
        factorizer.set_clauses(factor_clauses);
        factorizer.run();
        REQUIRE(factorizer.introduced_variable_count() == 1u);
        const auto pattern_literal = factorizer.last_pattern_literal();
        REQUIRE((pattern_literal.raw() == literal {variable {4u}, false}.raw()));

        scheduler.request_abort();
        REQUIRE(!scheduler.should_run());
        REQUIRE(scheduler.abort_requested());
        scheduler.clear_abort();
        scheduler.set_conflicts_seen(4096u);
        REQUIRE(scheduler.should_run());

        governor.set_current_usage(25u);
        REQUIRE(!scheduler.should_run());
        const auto epochs_before_pressure = scheduler.epoch_count();
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == epochs_before_pressure);
        REQUIRE(scheduler.last_budget() == 0u);
        REQUIRE(scheduler.last_passes_executed() == 0u);

        // removed std::cout: "simplify inprocess test passed\n";
    }

    TEST_CASE("inprocess scheduler executes real forward subsumption pass", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        cdcl::clause::database database;
        scheduler.attach_clause_database(database);

        scheduler.disable_pass("vivifier");
        scheduler.disable_pass("congruence");

        const std::array<literal, 1> clause_a {literal {variable {11u}, false}};
        const std::array<literal, 2> clause_b {literal {variable {11u}, false}, literal {variable {12u}, false}};
        database.add_clause(clause_a, false);
        database.add_clause(clause_b, false);

        scheduler.set_conflicts_seen(64u);
        scheduler.set_restart_count(0u);
        REQUIRE(scheduler.should_run());

        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_passes_executed() == 1u);
        REQUIRE(scheduler.last_effectiveness() >= 1u);
        const auto stats = database.stats_snapshot();
        REQUIRE(stats.irredundant_count == 1u);
    }

    TEST_CASE("inprocess soft memory policy skips heavy passes", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        scheduler.set_conflicts_seen(64u);
        scheduler.set_restart_count(1u);

        // Soft breach keeps inprocess enabled but should skip heavy passes for this epoch.
        governor.set_current_usage(11u);
        REQUIRE(scheduler.should_run());
        scheduler.run_epoch();
        scheduler.report_epoch_summary();

        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_passes_executed() == 1u);
        const auto& reported = scheduler.last_reported_summaries();
        REQUIRE(reported.size() == 3u);
        const auto vivifier_summary =
            std::find_if(reported.begin(), reported.end(),
                         [](const scheduler::inprocess::pass_summary& summary) noexcept { return summary.pass_name == "vivifier"; });
        REQUIRE(vivifier_summary != reported.end());
        REQUIRE_FALSE(vivifier_summary->executed);
        REQUIRE(vivifier_summary->skipped_by_memory_policy);

        // Reset to a non-breached level and advance trigger window, then all baseline passes can run.
        governor.set_current_usage(0u);
        scheduler.set_conflicts_seen(96u);
        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.last_passes_executed() == 3u);
    }

    TEST_CASE("inprocess proof-format gating skips congruence without veripb", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        proof_manager proof_manager;
        proof_manager.enable_format("drat");
        scheduler.attach_proof_manager(proof_manager);

        for (const auto pass_name: scheduler::inprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("congruence");

        scheduler.set_conflicts_seen(64u);
        scheduler.set_restart_count(0u);
        scheduler.run_epoch();
        scheduler.report_epoch_summary();

        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_passes_executed() == 0u);
        REQUIRE(scheduler.last_reported_summaries().size() == 1u);
        REQUIRE(scheduler.last_reported_summaries()[0].pass_name == "congruence");
        REQUIRE_FALSE(scheduler.last_reported_summaries()[0].executed);
        REQUIRE(scheduler.last_reported_summaries()[0].skipped_by_proof_format);

        proof_manager.disable_all();
        proof_manager.enable_format("veripb");
        scheduler.set_conflicts_seen(96u);
        scheduler.run_epoch();
        scheduler.report_epoch_summary();

        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.last_reported_summaries().size() == 1u);
        REQUIRE(scheduler.last_reported_summaries()[0].pass_name == "congruence");
        REQUIRE(scheduler.last_reported_summaries()[0].executed);
        REQUIRE_FALSE(scheduler.last_reported_summaries()[0].skipped_by_proof_format);
    }

    TEST_CASE("inprocess adaptive ordering prioritizes effective passes", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        cdcl::clause::database database;
        scheduler.attach_clause_database(database);

        for (const auto pass_name: scheduler::inprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("vivifier");
        scheduler.enable_pass("forward_subsumer");

        const std::array<literal, 1> clause_a {literal {variable {31u}, false}};
        const std::array<literal, 2> clause_b {literal {variable {31u}, false}, literal {variable {32u}, false}};
        database.add_clause(clause_a, false);
        database.add_clause(clause_b, false);

        scheduler.set_conflicts_seen(64u);
        scheduler.set_restart_count(0u);
        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_execution_order().size() == 2u);
        REQUIRE(scheduler.last_execution_order()[0] == "vivifier");
        REQUIRE(scheduler.last_execution_order()[1] == "forward_subsumer");
        REQUIRE(scheduler.pass_effectiveness_of("vivifier").observed_runs >= 1u);
        REQUIRE(scheduler.pass_effectiveness_of("forward_subsumer").observed_runs >= 1u);
        REQUIRE(scheduler.pass_effectiveness_of("forward_subsumer").effective_runs >= 1u);

        scheduler.set_conflicts_seen(96u);
        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.last_execution_order().size() == 2u);
        REQUIRE(scheduler.last_execution_order()[0] == "forward_subsumer");
        REQUIRE(scheduler.last_execution_order()[1] == "vivifier");
    }

    TEST_CASE("inprocess adaptive pass cap throttles on low yield and recovers on gain", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        cdcl::clause::database database;
        scheduler.attach_clause_database(database);

        scheduler.set_conflicts_seen(64u);
        scheduler.set_restart_count(0u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_passes_executed() == 3u);
        REQUIRE(scheduler.adaptive_pass_cap() == 3u);

        scheduler.set_conflicts_seen(96u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.last_passes_executed() == 3u);
        REQUIRE(scheduler.adaptive_pass_cap() == 2u);

        scheduler.set_conflicts_seen(128u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 2u);

        const std::array<literal, 1> clause_a {literal {variable {51u}, false}};
        const std::array<literal, 2> clause_b {literal {variable {51u}, false}, literal {variable {52u}, false}};
        database.add_clause(clause_a, false);
        database.add_clause(clause_b, false);

        scheduler.set_conflicts_seen(160u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 3u);
        REQUIRE(scheduler.last_passes_executed() == 2u);

        scheduler.set_conflicts_seen(224u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 4u);
        REQUIRE(scheduler.adaptive_pass_cap() == 3u);

        scheduler.set_conflicts_seen(256u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 5u);
        REQUIRE(scheduler.last_passes_executed() == 3u);
    }

    TEST_CASE("inprocess adaptive cooldown widens and shrinks trigger windows", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        cdcl::clause::database database;
        scheduler.attach_clause_database(database);

        for (const auto pass_name: scheduler::inprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("forward_subsumer");

        scheduler.set_conflicts_seen(64u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.conflict_trigger_window() == 32u);
        REQUIRE(scheduler.restart_trigger_window() == 1u);

        scheduler.set_conflicts_seen(96u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.conflict_trigger_window() == 64u);
        REQUIRE(scheduler.restart_trigger_window() == 2u);

        scheduler.set_conflicts_seen(128u);
        REQUIRE(!scheduler.should_run());

        const std::array<literal, 1> clause_a {literal {variable {71u}, false}};
        const std::array<literal, 2> clause_b {literal {variable {71u}, false}, literal {variable {72u}, false}};
        database.add_clause(clause_a, false);
        database.add_clause(clause_b, false);

        scheduler.set_conflicts_seen(160u);
        REQUIRE(scheduler.should_run());
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 3u);
        REQUIRE(scheduler.conflict_trigger_window() == 32u);
        REQUIRE(scheduler.restart_trigger_window() == 1u);
    }

    TEST_CASE("inprocess telemetry adjusts budget bonus", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        cdcl::clause::database database;
        scheduler.attach_clause_database(database);

        for (const auto pass_name: scheduler::inprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("forward_subsumer");

        const std::array<literal, 1> clause_a {literal {variable {91u}, false}};
        const std::array<literal, 2> clause_b {literal {variable {91u}, false}, literal {variable {92u}, false}};
        database.add_clause(clause_a, false);
        database.add_clause(clause_b, false);

        scheduler.set_conflicts_seen(64u);
        scheduler.set_decisions_seen(64u);
        scheduler.set_restart_count(0u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_structural_gain() >= 1u);
        REQUIRE(scheduler.conflict_density_ema() >= 0.5);
        REQUIRE(scheduler.telemetry_budget_bonus() >= 1u);

        scheduler.set_conflicts_seen(96u);
        scheduler.set_decisions_seen(96u);
        scheduler.run_epoch();
        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.last_budget() >= 5u);
    }

    TEST_CASE("inprocess telemetry uses restart and reduction pressure", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        cdcl::clause::database database;
        scheduler.attach_clause_database(database);

        for (const auto pass_name: scheduler::inprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("forward_subsumer");

        scheduler.set_conflicts_seen(64u);
        scheduler.set_decisions_seen(1024u);
        scheduler.set_restart_count(64u);
        scheduler.set_reduction_passes_seen(64u);
        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_structural_gain() == 0u);
        REQUIRE(scheduler.conflict_density_ema() < 0.10);
        REQUIRE(scheduler.restart_pressure_ema() >= 0.03);
        REQUIRE(scheduler.reduction_pressure_ema() >= 0.03);
        REQUIRE(scheduler.telemetry_budget_bonus() == 2u);

        scheduler.set_conflicts_seen(96u);
        scheduler.set_decisions_seen(2048u);
        scheduler.set_restart_count(96u);
        scheduler.set_reduction_passes_seen(96u);
        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.last_budget() >= 8u);
    }

    TEST_CASE("inprocess telemetry uses learned-clause pressure", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        scheduler::inprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        cdcl::clause::database database;
        scheduler.attach_clause_database(database);

        for (const auto pass_name: scheduler::inprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("forward_subsumer");

        scheduler.set_conflicts_seen(64u);
        scheduler.set_decisions_seen(4096u);
        scheduler.set_restart_count(0u);
        scheduler.set_reduction_passes_seen(0u);
        scheduler.set_learned_clauses_seen(64u);
        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 1u);
        REQUIRE(scheduler.last_structural_gain() == 0u);
        REQUIRE(scheduler.conflict_density_ema() < 0.10);
        REQUIRE(scheduler.learned_clause_pressure_ema() >= 1.0);
        REQUIRE(scheduler.telemetry_budget_bonus() == 2u);

        scheduler.set_conflicts_seen(96u);
        scheduler.set_decisions_seen(8192u);
        scheduler.set_learned_clauses_seen(96u);
        scheduler.run_epoch();

        REQUIRE(scheduler.epoch_count() == 2u);
        REQUIRE(scheduler.last_budget() >= 6u);
    }

} // namespace
