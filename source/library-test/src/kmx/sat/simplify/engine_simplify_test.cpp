#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/engine/congruence.hpp>
#include <kmx/sat/simplify/engine/decomposition.hpp>
#include <kmx/sat/simplify/engine/instantiation.hpp>
#include <kmx/sat/simplify/engine/probing.hpp>
#include <kmx/sat/simplify/engine/sweep.hpp>
#include <kmx/sat/simplify/equivalence_substitutor.hpp>
#include <kmx/sat/simplify/transitive_reducer.hpp>

namespace kmx::sat::simplify
{

    TEST_CASE("engine simplify", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        equivalence_substitutor decomposition_substitutor;
        engine::decomposition decomposition;
        decomposition.attach_substitutor(decomposition_substitutor);
        decomposition.add_implication(1u, 2u);
        decomposition.add_implication(2u, 1u);
        decomposition.add_implication(3u, 4u);
        decomposition.add_implication(4u, 3u);
        decomposition.add_implication(2u, 3u);
        decomposition.run_scc();
        decomposition.find_equivalences();
        decomposition.emit_substitutions();
        REQUIRE(decomposition.node_count() == 4u);
        REQUIRE(decomposition.component_count() == 2u);
        REQUIRE(decomposition.equivalence_count() == 2u);
        REQUIRE(decomposition.substitution_count() == 2u);
        REQUIRE(decomposition_substitutor.pending_rewrite_count() == 2u);

        engine::probing probing;
        const literal lit {variable {4u}, false};
        probing.probe_literal(lit);
        probing.run_failed_literal_probing();
        probing.learn_hyper_binary();
        probing.record_backbone_candidate(lit);
        REQUIRE(probing.probe_count() == 1u);
        REQUIRE(probing.hyper_binary_count() == 1u);
        REQUIRE(probing.backbone_candidate_count() == 1u);

        engine::probing database_probing;
        cdcl::clause::database probing_database;
        proof_manager probing_proof_manager;
        probing_proof_manager.enable_format("drat");
        const literal unit_lit {variable {8u}, false};
        const literal implied_lit {variable {9u}, false};
        const auto unit_ref = probing_database.add_clause(std::array<literal, 1> {unit_lit}, false);
        const auto implied_ref = probing_database.add_clause(std::array<literal, 2> {unit_lit.negated(), implied_lit}, false);
        probing_proof_manager.on_add_original(unit_ref, std::array<literal, 1> {unit_lit});
        probing_proof_manager.on_add_original(implied_ref, std::array<literal, 2> {unit_lit.negated(), implied_lit});
        database_probing.attach_database(probing_database);
        database_probing.attach_proof_manager(probing_proof_manager);
        database_probing.run_failed_literal_probing();
        const auto shrunk_clause = probing_database.storage_of().literals_of(implied_ref);
        REQUIRE(shrunk_clause.size() == 1u);
        REQUIRE(shrunk_clause[0] == implied_lit);
        REQUIRE(database_probing.hyper_binary_count() == 1u);
        REQUIRE(database_probing.backbone_candidate_count() == 1u);
        REQUIRE(probing_proof_manager.last_event().kind == proof::event_kind::shrink_clause);
        REQUIRE(probing_proof_manager.last_event().literals.size() == 1u);
        REQUIRE(probing_proof_manager.last_event().literals[0] == 9);

        transitive_reducer reducer;
        cdcl::clause::database transitive_database;
        const auto implication_ab =
            transitive_database.add_clause(std::array<literal, 2> {literal {variable {1u}, true}, literal {variable {2u}, false}}, false);
        const auto implication_bc =
            transitive_database.add_clause(std::array<literal, 2> {literal {variable {2u}, true}, literal {variable {3u}, false}}, false);
        const auto implication_ac =
            transitive_database.add_clause(std::array<literal, 2> {literal {variable {1u}, true}, literal {variable {3u}, false}}, false);
        reducer.attach_database(transitive_database);
        reducer.run();
        reducer.prune_binary_edges();
        reducer.report_removed_edges();
        REQUIRE(reducer.pruned());
        REQUIRE(reducer.removed_edge_count() == 1u);
        REQUIRE(transitive_database.stats_snapshot().irredundant_count == 2u);
        REQUIRE(transitive_database.storage_of().is_alive(implication_ab));
        REQUIRE(transitive_database.storage_of().is_alive(implication_bc));
        REQUIRE_FALSE(transitive_database.storage_of().is_alive(implication_ac));

        engine::congruence congruence;
        cdcl::clause::database congruence_clause_database;
        cdcl::bank::watch_list congruence_watch_list;
        cdcl::variable_mapper congruence_mapper;
        const variable ext_a {21u};
        const variable ext_b {22u};
        const auto internal_a = congruence_mapper.ensure_external_variable(ext_a);
        const auto internal_b = congruence_mapper.ensure_external_variable(ext_b);

        const literal lit_a_pos {internal_a, false};
        const literal lit_b_neg {internal_b, true};
        const std::array<literal, 2> congruence_clause {lit_a_pos, lit_b_neg};
        const auto congruence_clause_ref = congruence_clause_database.add_clause(congruence_clause, true);

        cdcl::watch congruence_watch {lit_a_pos, congruence_clause_ref, true};
        congruence_watch.set_binary_literal(lit_b_neg);
        congruence_watch_list.watch_literal(lit_b_neg, congruence_watch);

        congruence.attach_clause_database(congruence_clause_database);
        congruence.attach_watch_list(congruence_watch_list);
        congruence.attach_variable_mapper(congruence_mapper);
        congruence.gate_extractor().find_and_gate(10u, 3u, 5u);
        congruence.gate_extractor().find_and_gate(internal_b.index(), internal_a.index(), internal_a.index());
        congruence.gate_extractor().materialize_gate_summary();
        congruence.apply_gate_constraints();
        congruence.derive_equivalences();
        REQUIRE(congruence.substitutor().pending_rewrite_count() == 1u);
        congruence.run();
        REQUIRE(congruence.gate_constraint_count() == 2u);
        REQUIRE(congruence.equivalence_count() == 1u);
        REQUIRE(congruence.run_completed());
        REQUIRE(congruence.substitutor().rewrite_completed());
        REQUIRE(congruence.substitutor().last_applied_rewrite_count() == 2u);

        const auto rewritten_literals = congruence_clause_database.storage_of().literals_of(congruence_clause_ref);
        REQUIRE(rewritten_literals.size() == 2u);
        REQUIRE(rewritten_literals[0].variable_of().index() == internal_a.index());
        REQUIRE(rewritten_literals[1].variable_of().index() == internal_a.index());

        REQUIRE(congruence_watch_list.size_of(lit_b_neg) == 0u);
        const literal lit_a_neg {internal_a, true};
        REQUIRE(congruence_watch_list.size_of(lit_a_neg) == 1u);

        const auto remapped_internal_b = congruence_mapper.to_internal_literal(literal {ext_b, false});
        REQUIRE(remapped_internal_b.variable_of().index() == internal_a.index());

        engine::instantiation instantiation;
        instantiation.collect_candidates();
        instantiation.instantiate_literal_removal();
        instantiation.run();
        REQUIRE(instantiation.candidate_count() == 1u);
        REQUIRE(instantiation.removal_count() == 1u);
        REQUIRE(instantiation.run_completed());

        engine::sweep sweep;
        sweep.build_micro_instance();
        sweep.extract_backbone();
        sweep.extract_equivalences();
        sweep.transfer_facts();
        sweep.run();
        REQUIRE(sweep.micro_instance_built());
        REQUIRE(sweep.backbone_count() == 1u);
        REQUIRE(sweep.equivalence_count() == 1u);
        REQUIRE(sweep.transferred());

        // removed std::cout: "engine simplify test passed\n";
    }

} // namespace
