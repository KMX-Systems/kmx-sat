#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/scheduler/preprocess.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    TEST_CASE("preprocess scheduler runs opt-in sweep pass", "[sat]")
    {
        scheduler::preprocess scheduler;
        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("sweep");

        scheduler.run_initial_pipeline();

        REQUIRE(scheduler.enabled_pass_count() == 1u);
        REQUIRE(scheduler.executed_pass_count() == 1u);
    }

    TEST_CASE("preprocess pass ids have stable text projections", "[sat]")
    {
        for (std::uint8_t value {}; value <= static_cast<std::uint8_t>(scheduler::preprocess::pass_id::sweep); ++value)
        {
            const auto id = static_cast<scheduler::preprocess::pass_id>(value);
            REQUIRE(!scheduler::preprocess::pass_name(id).empty());
            REQUIRE(parse_pass(scheduler::preprocess::pass_name(id)).has_value());
            REQUIRE(parse_pass(scheduler::preprocess::pass_name(id)).value() == id);
        }
        REQUIRE_FALSE(parse_pass("not_a_pass").has_value());
    }

    TEST_CASE("preprocess scheduler runs enabled passes and reports summaries", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        REQUIRE(!scheduler.should_abort_pipeline());
        REQUIRE(scheduler.enabled_pass_count() > 0u);

        scheduler.disable_pass("vivifier");
        const auto enabled_after_disable = scheduler.enabled_pass_count();
        REQUIRE(enabled_after_disable > 0u);

        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        REQUIRE(scheduler.pipeline_run_count() == 1u);
        REQUIRE(scheduler.executed_pass_count() == enabled_after_disable);
        REQUIRE(scheduler.reported_summary_count() == 1u);
        REQUIRE(scheduler.last_reported_summaries().size() == enabled_after_disable);
        REQUIRE(std::all_of(scheduler.last_reported_summaries().begin(), scheduler.last_reported_summaries().end(),
                            [](const scheduler::preprocess::pass_summary& summary) noexcept
                            { return summary.executed && !summary.skipped_by_selector; }));
        REQUIRE(scheduler::preprocess::pass_name_of(scheduler.last_reported_summaries().front()) == "transitive_reducer");

        scheduler.enable_pass("vivifier");
        REQUIRE(scheduler.enabled_pass_count() == enabled_after_disable + 1u);

        scheduler.request_abort();
        REQUIRE(scheduler.should_abort_pipeline());
        REQUIRE(scheduler.abort_requested());
        scheduler.clear_abort();
        REQUIRE(!scheduler.should_abort_pipeline());

        governor.set_current_usage(25u);
        REQUIRE(scheduler.should_abort_pipeline());

        const auto executed_before_abort = scheduler.executed_pass_count();
        scheduler.run_initial_pipeline();
        REQUIRE(scheduler.executed_pass_count() == 0u);
        REQUIRE(executed_before_abort > 0u);
    }

    TEST_CASE("preprocess decomposition pass rewrites attached clause watch and mapper sinks", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::clause::database clause_database;
        cdcl::bank::watch_list watch_list;
        cdcl::variable_mapper mapper;

        scheduler.attach_clause_database(clause_database);
        scheduler.attach_watch_list(watch_list);
        scheduler.attach_variable_mapper(mapper);

        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("decomposition");

        const variable var_three {3u};
        const variable var_five {5u};
        const literal lit_three_pos {var_three, false};
        const literal lit_three_neg {var_three, true};
        const literal lit_five_pos {var_five, false};
        const literal lit_five_neg {var_five, true};

        const std::array<literal, 2> clause_literals {lit_five_pos, lit_three_neg};
        const auto clause_ref = clause_database.add_clause(clause_literals, false);

        // A binary watch carries the clause's other literal as its blocking literal; there is no second field.
        cdcl::watch entry {lit_five_neg, clause_ref, true};
        watch_list.watch_literal(lit_five_pos, entry);

        const variable external_var {17u};
        const variable internal_var = mapper.ensure_external_variable(external_var);

        scheduler.decomposition_engine().add_implication(var_five.index(), var_three.index());
        scheduler.decomposition_engine().add_implication(var_three.index(), var_five.index());
        scheduler.decomposition_engine().add_implication(internal_var.index(), var_three.index());
        scheduler.decomposition_engine().add_implication(var_three.index(), internal_var.index());

        scheduler.run_initial_pipeline();

        REQUIRE(scheduler.executed_pass_count() == 1u);

        const auto rewritten_clause = clause_database.storage_of().literals_of(clause_ref);
        REQUIRE(rewritten_clause.size() == 2u);
        REQUIRE(rewritten_clause[0] == lit_three_pos);
        REQUIRE(rewritten_clause[1] == lit_three_neg);

        REQUIRE(watch_list.size_of(lit_five_pos) == 0u);
        REQUIRE(watch_list.size_of(lit_three_pos) == 1u);

        std::vector<cdcl::watch> remapped_entries {};
        watch_list.iterate(lit_three_pos, [&](const cdcl::watch& watch_entry) noexcept { remapped_entries.push_back(watch_entry); });
        REQUIRE(remapped_entries.size() == 1u);
        REQUIRE(remapped_entries[0].blocking_literal() == lit_three_neg);
        REQUIRE(remapped_entries[0].binary_literal() == lit_three_neg);

        const auto remapped_internal = mapper.to_internal_literal(literal {external_var, false});
        REQUIRE(remapped_internal.variable_of().index() == var_three.index());
    }

    TEST_CASE("preprocess gate and congruence passes derive substitutions from clause patterns", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::clause::database clause_database;
        cdcl::bank::watch_list watch_list;
        cdcl::variable_mapper mapper;

        scheduler.attach_clause_database(clause_database);
        scheduler.attach_watch_list(watch_list);
        scheduler.attach_variable_mapper(mapper);

        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("gate");
        scheduler.enable_pass("congruence");

        const variable external_a {41u};
        const variable external_output {43u};
        const variable internal_a = mapper.ensure_external_variable(external_a);
        const variable internal_output = mapper.ensure_external_variable(external_output);

        const literal lit_a_pos {internal_a, false};
        const literal lit_o_pos {internal_output, false};
        const literal lit_a_neg {internal_a, true};
        const literal lit_o_neg {internal_output, true};

        const std::array<literal, 3> and_guard_clause {lit_a_neg, lit_a_neg, lit_o_pos};
        const std::array<literal, 2> and_left_clause {lit_a_pos, lit_o_neg};
        const std::array<literal, 2> rewrite_target_clause {lit_o_pos, lit_a_neg};

        clause_database.add_clause(and_guard_clause, false);
        clause_database.add_clause(and_left_clause, false);
        const auto rewrite_ref = clause_database.add_clause(rewrite_target_clause, false);

        cdcl::watch watch_entry {lit_o_neg, rewrite_ref, true};
        watch_list.watch_literal(lit_o_pos, watch_entry);

        scheduler.run_initial_pipeline();

        REQUIRE(scheduler.executed_pass_count() == 2u);
        REQUIRE(scheduler.congruence_engine().gate_extractor().summarized_gate_count() >= 1u);

        const auto rewritten_clause = clause_database.storage_of().literals_of(rewrite_ref);
        REQUIRE(rewritten_clause.size() == 2u);
        REQUIRE(rewritten_clause[0].variable_of().index() == internal_a.index());
        REQUIRE(rewritten_clause[1].variable_of().index() == internal_a.index());

        REQUIRE(watch_list.size_of(lit_o_pos) == 0u);
        REQUIRE(watch_list.size_of(lit_a_pos) == 1u);

        std::vector<cdcl::watch> remapped_watches {};
        watch_list.iterate(lit_a_pos, [&](const cdcl::watch& entry) noexcept { remapped_watches.push_back(entry); });
        REQUIRE(remapped_watches.size() == 1u);
        REQUIRE(remapped_watches[0].blocking_literal() == lit_a_neg);
        REQUIRE(remapped_watches[0].binary_literal() == lit_a_neg);

        const auto remapped_output = mapper.to_internal_literal(literal {external_output, false});
        REQUIRE(remapped_output.variable_of().index() == internal_a.index());
    }

    TEST_CASE("preprocess selector skips memory heavy passes under soft pressure", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        governor.set_current_usage(11u);
        // Congruence left the default pipeline; it is enabled here so both memory-heavy passes are under test.
        scheduler.enable_pass("congruence");
        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        const auto expected_skipped = std::size_t {2u};
        REQUIRE(scheduler.executed_pass_count() + expected_skipped == scheduler::preprocess::baseline_passes.size() + 1u);
        REQUIRE(scheduler.profile_selector().fingerprint_count() == 1u);
        REQUIRE(scheduler.profile_selector().pass_plan_count() == 1u);
        REQUIRE(scheduler.profile_selector().policy_update_count() == 1u);
        REQUIRE(scheduler.profile_selector().effectiveness_count() == scheduler.executed_pass_count());
        REQUIRE(scheduler.profile_selector().current_pass_plan().skip_memory_heavy_passes);

        const auto& first_summaries = scheduler.last_reported_summaries();
        REQUIRE(first_summaries.size() == scheduler::preprocess::baseline_passes.size() + 1u);
        const auto vivifier_summary =
            std::find_if(first_summaries.begin(), first_summaries.end(), [](const scheduler::preprocess::pass_summary& summary) noexcept
                         { return summary.id == scheduler::preprocess::pass_id::vivifier; });
        REQUIRE(vivifier_summary != first_summaries.end());
        REQUIRE_FALSE(vivifier_summary->executed);
        REQUIRE(vivifier_summary->skipped_by_selector);

        const auto congruence_summary =
            std::find_if(first_summaries.begin(), first_summaries.end(), [](const scheduler::preprocess::pass_summary& summary) noexcept
                         { return summary.id == scheduler::preprocess::pass_id::congruence; });
        REQUIRE(congruence_summary != first_summaries.end());
        REQUIRE_FALSE(congruence_summary->executed);
        REQUIRE(congruence_summary->skipped_by_selector);

        governor.set_current_usage(0u);
        scheduler.run_initial_pipeline();

        // Nine default passes plus the explicitly enabled congruence pass.
        const auto enabled_passes = scheduler::preprocess::baseline_passes.size() + 1u;
        REQUIRE(scheduler.executed_pass_count() == enabled_passes);
        REQUIRE(scheduler.profile_selector().fingerprint_count() == 2u);
        REQUIRE(scheduler.profile_selector().pass_plan_count() == 2u);
        REQUIRE(scheduler.profile_selector().policy_update_count() == 2u);
        REQUIRE(scheduler.profile_selector().effectiveness_count() == enabled_passes + (enabled_passes - expected_skipped));
        REQUIRE(!scheduler.profile_selector().current_pass_plan().skip_memory_heavy_passes);
    }

    TEST_CASE("preprocess selector runs the factorizer on binary dense formulas", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::clause::database clause_database;
        scheduler.attach_clause_database(clause_database);

        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("probing");
        scheduler.enable_pass("factorizer");
        scheduler.enable_pass("transitive_reducer");

        for (std::uint32_t index {1u}; index <= 16u; ++index)
        {
            const variable left {index};
            const variable right {index + 100u};
            const std::array<literal, 2> binary_clause {literal {left, false}, literal {right, true}};
            clause_database.add_clause(binary_clause, false);
        }

        scheduler.run_initial_pipeline();

        // Binary-dense formulas are where factoring pays (at-most-one constraints), so nothing gates it here.
        REQUIRE(scheduler.executed_pass_count() == 3u);
        REQUIRE(!scheduler.profile_selector().current_pass_plan().skip_memory_heavy_passes);

        const auto& fingerprint = scheduler.profile_selector().current_formula_fingerprint();
        REQUIRE(fingerprint.clause_count == 16u);
        REQUIRE(fingerprint.binary_clause_count == 16u);
        REQUIRE(fingerprint.binary_clause_ratio == 1.0);
    }

    TEST_CASE("preprocess proof-format gating skips gate-level passes without veripb", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        proof_manager proof_manager;
        proof_manager.enable_format("drat");
        scheduler.attach_proof_manager(proof_manager);

        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("gate");
        scheduler.enable_pass("congruence");

        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        REQUIRE(scheduler.executed_pass_count() == 0u);
        REQUIRE(scheduler.last_reported_summaries().size() == 2u);
        REQUIRE(std::all_of(scheduler.last_reported_summaries().begin(), scheduler.last_reported_summaries().end(),
                            [](const scheduler::preprocess::pass_summary& summary) noexcept
                            { return !summary.executed && summary.skipped_by_proof_format; }));

        proof_manager.disable_all();
        proof_manager.enable_format("veripb");
        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        REQUIRE(scheduler.executed_pass_count() == 2u);
        REQUIRE(std::all_of(scheduler.last_reported_summaries().begin(), scheduler.last_reported_summaries().end(),
                            [](const scheduler::preprocess::pass_summary& summary) noexcept
                            { return summary.executed && !summary.skipped_by_proof_format; }));
    }

    TEST_CASE("preprocess pipeline wires transitive reducer to clause database", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::clause::database clause_database;
        scheduler.attach_clause_database(clause_database);

        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("transitive_reducer");

        const auto implication_ab =
            clause_database.add_clause(std::array<literal, 2> {literal {variable {1u}, true}, literal {variable {2u}, false}}, false);
        const auto implication_bc =
            clause_database.add_clause(std::array<literal, 2> {literal {variable {2u}, true}, literal {variable {3u}, false}}, false);
        const auto implication_ac =
            clause_database.add_clause(std::array<literal, 2> {literal {variable {1u}, true}, literal {variable {3u}, false}}, false);

        scheduler.run_initial_pipeline();

        REQUIRE(scheduler.executed_pass_count() == 1u);
        REQUIRE(clause_database.stats_snapshot().irredundant_count == 2u);
        REQUIRE(clause_database.storage_of().is_alive(implication_ab));
        REQUIRE(clause_database.storage_of().is_alive(implication_bc));
        REQUIRE_FALSE(clause_database.storage_of().is_alive(implication_ac));
    }

    TEST_CASE("preprocess selector skips probing on long clause heavy formulas", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::clause::database clause_database;
        scheduler.attach_clause_database(clause_database);

        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("probing");
        scheduler.enable_pass("factorizer");
        scheduler.enable_pass("transitive_reducer");

        for (std::uint32_t index {1u}; index <= 16u; ++index)
        {
            const std::array<literal, 5> long_clause {literal {variable {index}, false}, literal {variable {index + 100u}, false},
                                                      literal {variable {index + 200u}, false}, literal {variable {index + 300u}, false},
                                                      literal {variable {index + 400u}, false}};
            clause_database.add_clause(long_clause, false);
        }

        scheduler.run_initial_pipeline();

        REQUIRE(scheduler.executed_pass_count() == 2u);
        REQUIRE(scheduler.profile_selector().current_pass_plan().skip_probing_for_long_clause_heavy);

        const auto& fingerprint = scheduler.profile_selector().current_formula_fingerprint();
        REQUIRE(fingerprint.clause_count == 16u);
        REQUIRE(fingerprint.long_clause_count == 16u);
        REQUIRE(fingerprint.long_clause_ratio == 1.0);
        REQUIRE(fingerprint.binary_clause_ratio == 0.0);
    }

    TEST_CASE("preprocess selector uses inprocess pressure to skip memory heavy passes", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        governor.set_current_usage(0u);
        scheduler.enable_pass("congruence");
        scheduler.set_inprocess_telemetry_snapshot(0.30, 0.01, 0.04, 0.04, 0.10);
        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        const auto& plan = scheduler.profile_selector().current_pass_plan();
        REQUIRE(plan.skip_memory_heavy_from_inprocess_pressure);
        REQUIRE_FALSE(plan.skip_memory_heavy_from_learned_clause_pressure);
        REQUIRE(plan.skip_memory_heavy_passes);

        const auto& summaries = scheduler.last_reported_summaries();
        const auto vivifier_summary =
            std::find_if(summaries.begin(), summaries.end(), [](const scheduler::preprocess::pass_summary& summary) noexcept
                         { return summary.id == scheduler::preprocess::pass_id::vivifier; });
        REQUIRE(vivifier_summary != summaries.end());
        REQUIRE_FALSE(vivifier_summary->executed);
        REQUIRE(vivifier_summary->skipped_by_selector);

        const auto congruence_summary =
            std::find_if(summaries.begin(), summaries.end(), [](const scheduler::preprocess::pass_summary& summary) noexcept
                         { return summary.id == scheduler::preprocess::pass_id::congruence; });
        REQUIRE(congruence_summary != summaries.end());
        REQUIRE_FALSE(congruence_summary->executed);
        REQUIRE(congruence_summary->skipped_by_selector);
    }

    TEST_CASE("preprocess selector uses learned-clause pressure from inprocess telemetry", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        governor.set_current_usage(0u);
        scheduler.set_inprocess_telemetry_snapshot(0.01, 0.01, 0.00, 0.00, 0.80);
        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        const auto& plan = scheduler.profile_selector().current_pass_plan();
        REQUIRE(plan.skip_memory_heavy_from_inprocess_pressure);
        REQUIRE(plan.skip_memory_heavy_from_learned_clause_pressure);
        REQUIRE(plan.skip_memory_heavy_passes);

        const auto& summaries = scheduler.last_reported_summaries();
        const auto vivifier_summary =
            std::find_if(summaries.begin(), summaries.end(), [](const scheduler::preprocess::pass_summary& summary) noexcept
                         { return summary.id == scheduler::preprocess::pass_id::vivifier; });
        REQUIRE(vivifier_summary != summaries.end());
        REQUIRE_FALSE(vivifier_summary->executed);
        REQUIRE(vivifier_summary->skipped_by_selector);
    }

    TEST_CASE("preprocess selector keeps heavy passes when inprocess structural gain is healthy", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::memory_governor governor;
        governor.register_budget(10u, 20u);
        scheduler.attach_memory_governor(governor);

        governor.set_current_usage(0u);
        scheduler.enable_pass("congruence");
        scheduler.set_inprocess_telemetry_snapshot(0.01, 0.50, 0.00, 0.00, 0.80);
        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        const auto& plan = scheduler.profile_selector().current_pass_plan();
        REQUIRE_FALSE(plan.skip_memory_heavy_from_inprocess_pressure);
        REQUIRE_FALSE(plan.skip_memory_heavy_from_learned_clause_pressure);
        REQUIRE_FALSE(plan.skip_memory_heavy_passes);

        const auto& summaries = scheduler.last_reported_summaries();
        const auto vivifier_summary =
            std::find_if(summaries.begin(), summaries.end(), [](const scheduler::preprocess::pass_summary& summary) noexcept
                         { return summary.id == scheduler::preprocess::pass_id::vivifier; });
        REQUIRE(vivifier_summary != summaries.end());
        REQUIRE(vivifier_summary->executed);
        REQUIRE_FALSE(vivifier_summary->skipped_by_selector);

        const auto congruence_summary =
            std::find_if(summaries.begin(), summaries.end(), [](const scheduler::preprocess::pass_summary& summary) noexcept
                         { return summary.id == scheduler::preprocess::pass_id::congruence; });
        REQUIRE(congruence_summary != summaries.end());
        REQUIRE(congruence_summary->executed);
        REQUIRE_FALSE(congruence_summary->skipped_by_selector);
    }

    TEST_CASE("preprocess probing forwards backbone candidates into clause state and proof events", "[sat]")
    {
        using namespace kmx::sat;

        scheduler::preprocess scheduler;
        cdcl::clause::database clause_database;
        proof_manager proof_manager;
        proof_manager.enable_format("drat");

        scheduler.attach_clause_database(clause_database);
        scheduler.attach_proof_manager(proof_manager);

        for (const auto pass_name: scheduler::preprocess::baseline_passes)
            scheduler.disable_pass(pass_name);
        scheduler.enable_pass("probing");

        const literal unit_lit {variable {61u}, false};
        const literal implied_lit {variable {62u}, false};
        const auto unit_ref = clause_database.add_clause(std::array<literal, 1> {unit_lit}, false);
        const auto binary_ref = clause_database.add_clause(std::array<literal, 2> {unit_lit.negated(), implied_lit}, false);

        scheduler.run_initial_pipeline();

        REQUIRE(scheduler.executed_pass_count() == 1u);
        REQUIRE(clause_database.storage_of().literals_of(binary_ref).size() == 1u);
        REQUIRE(clause_database.storage_of().literals_of(binary_ref)[0] == implied_lit);
        REQUIRE(clause_database.stats_snapshot().redundant_count == 0u);
        REQUIRE(clause_database.storage_of().literals_of(unit_ref).size() == 1u);
        bool attached_backbone_unit {};
        clause_database.iterate_redundant(
            [&](const cdcl::clause::ref_t ref) noexcept
            {
                const auto clause = clause_database.storage_of().literals_of(ref);
                if (clause.size() == 1u && clause.front() == implied_lit)
                    attached_backbone_unit = true;
            });
        REQUIRE_FALSE(attached_backbone_unit);
        REQUIRE(proof_manager.last_event().kind == proof::event_kind::shrink_clause);
    }
}