#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/garbage_collector.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("garbage collector tracks collection lifecycle", "[sat]")
    {
        garbage_collector collector;

        REQUIRE(collector.should_collect() == false);

        collector.collect();
        collector.relocate_live_clause();
        collector.rewrite_watchers();
        collector.rewrite_reasons();
        collector.finalize_cycle();

        REQUIRE(collector.relocated_clause_count() == 0u);
        REQUIRE(collector.watch_rewrite_count() == 0u);
        REQUIRE(collector.reason_rewrite_count() == 0u);
        REQUIRE(collector.finalized() == true);
        REQUIRE(collector.should_collect() == false);
    }

    TEST_CASE("garbage collector relocates live clauses and rewrites watchers", "[sat]")
    {
        clause::database database;
        bank::watch_list watch_list;

        const literal watched_literal {variable {1u}, false};
        const literal blocking_literal {variable {2u}, false};
        const auto live_ref = database.add_clause(std::array<literal, 2> {watched_literal, blocking_literal}, true);
        database.set_glue(live_ref, 3u);
        database.increment_used_count(live_ref);
        database.increment_activity(live_ref, 5.0);
        const auto garbage_ref = database.add_clause(std::array<literal, 1> {blocking_literal}, true);
        database.mark_garbage(garbage_ref);
        proof_manager proof_manager;
        proof_manager.on_add_original(live_ref, database.storage_of().literals_of(live_ref));
        const auto stable_id_before = proof_manager.stable_id_for_clause(live_ref);
        store::clause_cold cold_store;
        cold_store.set_enabled(true);
        cold_store.demote_to_cold(live_ref, database.storage_of().literals_of(live_ref));
        watch_list.watch_literal(watched_literal, watch {blocking_literal, live_ref});

        store::assignment assignment;
        assignment.set_current_level(1u);
        assignment.set_current_trail_position(0u);
        assignment.assign(watched_literal, live_ref);

        garbage_collector collector;
        collector.attach_database(database);
        collector.attach_watch_list(watch_list);
        collector.attach_assignment_store(assignment);
        collector.attach_proof_manager(proof_manager);
        collector.attach_cold_store(cold_store);

        REQUIRE(collector.should_collect());

        collector.collect();
        collector.relocate_live_clause();
        collector.rewrite_watchers();
        collector.rewrite_reasons();
        collector.finalize_cycle();

        const auto relocated_ref = database.storage_of().resolve_ref(live_ref);
        REQUIRE(relocated_ref.valid());
        REQUIRE(relocated_ref != live_ref);
        REQUIRE(collector.relocated_clause_count() == 1u);
        REQUIRE(collector.watch_rewrite_count() == 1u);
        REQUIRE(collector.reason_rewrite_count() == 1u);
        REQUIRE(collector.proof_relocation_count() == 1u);
        REQUIRE(watch_list.contains(watched_literal, watch {blocking_literal, relocated_ref}));
        REQUIRE(assignment.reason_of(watched_literal.variable_of()) == relocated_ref);
        REQUIRE(database.storage_of().proof_id_of(relocated_ref).valid());
        REQUIRE(proof_manager.stable_id_for_clause(relocated_ref).equals(stable_id_before));
        REQUIRE_FALSE(cold_store.is_cold(live_ref));
        REQUIRE(cold_store.is_cold(relocated_ref));
        REQUIRE(cold_store.decode_literals(relocated_ref).size() == 2u);
        const auto quality = database.quality_of(relocated_ref);
        REQUIRE(quality.glue == 3u);
        REQUIRE(quality.used_count == 1u);
        REQUIRE(quality.activity == 5.0);
    }
}
