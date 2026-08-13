#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/eliminator/clause/blocked.hpp>
#include <kmx/sat/simplify/eliminator/clause/covered.hpp>
#include <kmx/sat/simplify/extractor/backbone.hpp>
#include <kmx/sat/simplify/flush_restore_manager.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/preprocessing_profile_selector.hpp>
#include <kmx/sat/simplify/vivifier.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{

    TEST_CASE("elimination policy", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::simplify;

        eliminator::clause::blocked blocked;
        blocked.run();
        REQUIRE(blocked.blocked_count() == 1u);
        const auto blocked_literal = blocked.last_blocked_literal();
        const auto expected_literal = literal {variable {2u}, true};
        REQUIRE(blocked_literal.raw() == expected_literal.raw());

        eliminator::clause::covered covered;
        covered.compute_covered_literals(cdcl::clause::ref_t {});
        covered.mark_covered(cdcl::clause::ref_t {});
        covered.run();
        REQUIRE(covered.covered_count() == 1u);

        cdcl::clause::database flush_database;
        const auto irredundant_ref =
            flush_database.add_clause(std::array<literal, 2> {literal {variable {7u}, false}, literal {variable {8u}, false}}, false);
        const auto redundant_ref =
            flush_database.add_clause(std::array<literal, 2> {literal {variable {9u}, false}, literal {variable {10u}, false}}, true);
        flush_database.mark_garbage(redundant_ref);

        flush_restore_manager flush_restore;
        flush_restore.attach_database(flush_database);
        flush_restore.flush_redundant();
        REQUIRE(flush_restore.last_flush_removed_count() == 1u);
        REQUIRE(flush_database.stats_snapshot().redundant_count == 0u);

        flush_restore.remove_satisfied([&](const cdcl::clause::ref_t ref) noexcept { return ref == irredundant_ref; });
        REQUIRE(flush_restore.last_flush_removed_count() == 1u);

        flush_restore.restore_irredundant_only();
        REQUIRE(flush_restore.restored_clause_count() == 1u);
        flush_restore.restore_all();
        REQUIRE(flush_restore.restored_clause_count() == 1u);
        REQUIRE(flush_restore.flush_count() == 1u);
        REQUIRE(flush_restore.restore_count() == 2u);
        REQUIRE(flush_restore.satisfied_removed_count() == 1u);
        REQUIRE(flush_database.stats_snapshot().irredundant_count == 1u);
        REQUIRE(flush_database.stats_snapshot().redundant_count == 1u);

        cdcl::clause::database subsumption_database;
        const auto subsuming_ref = subsumption_database.add_clause(std::array<literal, 1> {literal {variable {1u}, false}}, false);
        const auto subsumed_ref =
            subsumption_database.add_clause(std::array<literal, 2> {literal {variable {1u}, false}, literal {variable {2u}, false}}, false);

        forward_subsumer subsumer;
        proof_manager subsumer_proof_manager;
        subsumer_proof_manager.enable_format("drat");
        subsumer.attach_database(subsumption_database);
        subsumer.attach_proof_manager(subsumer_proof_manager);
        REQUIRE(subsumer.is_subsumed(subsumed_ref));
        subsumer.strengthen_subsumed_clause(subsumed_ref);
        REQUIRE(subsumer.strengthened_count() == 1u);
        REQUIRE(subsumer_proof_manager.last_event().kind == proof::event_kind::shrink_clause);
        subsumer.run();
        REQUIRE(subsumer.run_count() == 1u);
        REQUIRE(subsumer.subsumed_count() == 1u);
        REQUIRE(subsumer.last_subsumed_ref() == subsumed_ref);
        REQUIRE(subsumer_proof_manager.last_event().kind == proof::event_kind::delete_clause);
        REQUIRE(subsumption_database.storage_of().is_alive(subsuming_ref));
        REQUIRE(!subsumption_database.storage_of().is_alive(subsumed_ref));

        cdcl::clause::database backbone_database;
        proof_manager backbone_proof_manager;
        backbone_proof_manager.enable_format("drat");
        extractor::backbone backbone;
        const literal backbone_lit {variable {30u}, false};
        backbone.attach_database(backbone_database);
        backbone.attach_proof_manager(backbone_proof_manager);
        backbone.record_candidate(backbone_lit);
        backbone.confirm_candidate(backbone_lit);
        backbone.emit_unit_fact(backbone_lit);
        REQUIRE(backbone.candidate_count() == 1u);
        REQUIRE(backbone.confirmed_count() == 1u);
        REQUIRE(backbone.emitted_count() == 1u);
        REQUIRE(backbone.last_emitted_literal() == backbone_lit);
        REQUIRE(backbone_database.stats_snapshot().redundant_count == 1u);
        REQUIRE(backbone_proof_manager.last_event().kind == proof::event_kind::add_derived);
        backbone.reset();
        REQUIRE(backbone.candidate_count() == 0u);
        REQUIRE(backbone.confirmed_count() == 0u);
        REQUIRE(backbone.emitted_count() == 0u);
        backbone.record_candidate(backbone_lit);
        backbone.confirm_candidate(backbone_lit);
        backbone.emit_unit_fact(backbone_lit);
        REQUIRE(backbone.emitted_count() == 1u);
        REQUIRE(backbone_database.stats_snapshot().redundant_count == 1u);

        extractor::backbone guarded_backbone;
        cdcl::clause::database guarded_database;
        guarded_backbone.attach_database(guarded_database);
        const literal guarded_lit {variable {31u}, false};
        guarded_backbone.record_candidate(literal {});
        guarded_backbone.record_candidate(guarded_lit);
        guarded_backbone.record_candidate(guarded_lit.negated());
        REQUIRE(guarded_backbone.candidate_count() == 1u);
        guarded_backbone.confirm_candidate(guarded_lit);
        guarded_backbone.emit_unit_fact(guarded_lit);
        guarded_backbone.emit_unit_fact(guarded_lit);
        REQUIRE(guarded_backbone.emitted_count() == 1u);

        extractor::backbone existing_unit_backbone;
        cdcl::clause::database existing_unit_database;
        existing_unit_database.add_clause(std::array<literal, 1> {literal {variable {32u}, false}}, false);
        existing_unit_backbone.attach_database(existing_unit_database);
        const literal existing_unit {variable {32u}, false};
        existing_unit_backbone.record_candidate(existing_unit);
        existing_unit_backbone.confirm_candidate(existing_unit);
        existing_unit_backbone.emit_unit_fact(existing_unit);
        REQUIRE(existing_unit_backbone.emitted_count() == 1u);
        REQUIRE(existing_unit_database.stats_snapshot().irredundant_count == 1u);

        preprocessing_profile_selector selector;
        selector.fingerprint_formula();
        selector.select_pass_plan();
        selector.record_pass_effectiveness();
        selector.update_selection_policy();
        REQUIRE(selector.fingerprint_count() == 1u);
        REQUIRE(selector.pass_plan_count() == 1u);
        REQUIRE(selector.effectiveness_count() == 1u);
        REQUIRE(selector.policy_update_count() == 1u);

        vivifier viv;
        viv.vivify_clause(cdcl::clause::ref_t {1u});
        viv.commit_shrunk_clause(cdcl::clause::ref_t {1u});
        viv.run();
        REQUIRE(viv.vivified_clause_count() == 1u);
        REQUIRE(viv.committed_shrink_count() == 0u);
        REQUIRE(viv.run_completed());

        cdcl::clause::database vivifier_database;
        const auto vivifier_ref_a = vivifier_database.add_clause(
            std::array<literal, 3> {
                literal {variable {11u}, false},
                literal {variable {12u}, false},
                literal {variable {13u}, false},
            },
            false);
        const auto vivifier_ref_b = vivifier_database.add_clause(
            std::array<literal, 2> {
                literal {variable {14u}, false},
                literal {variable {15u}, false},
            },
            true);

        vivifier database_vivifier;
        database_vivifier.attach_database(vivifier_database);
        database_vivifier.set_budget(1u);
        database_vivifier.run();

        REQUIRE(database_vivifier.run_completed());
        REQUIRE(database_vivifier.clause_budget() == 1u);
        REQUIRE(database_vivifier.processed_in_current_run() == 1u);
        REQUIRE(database_vivifier.vivified_clause_count() == 1u);
        REQUIRE(database_vivifier.committed_shrink_count() == 0u);
        REQUIRE(vivifier_database.storage_of().literals_of(vivifier_ref_a).size() == 3u);
        REQUIRE(vivifier_database.storage_of().literals_of(vivifier_ref_b).size() == 2u);

        // removed std::cout: "elimination policy test passed\n";
    }

} // namespace
