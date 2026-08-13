#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/garbage_collector.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("repeated relocation preserves clause identity and payload state", "[sat]")
    {
        const literal watched_literal {variable {700u}, false};
        const literal blocking_literal {variable {701u}, false};
        const std::array<literal, 2> literals {watched_literal, blocking_literal};

        clause::database database;
        auto live_ref = database.add_clause(literals, true);
        database.set_glue(live_ref, 2u);
        database.increment_used_count(live_ref);
        database.increment_activity(live_ref, 6.0);

        bank::watch_list watch_list;
        watch_list.watch_literal(watched_literal, watch {blocking_literal, live_ref});

        store::assignment assignment;
        assignment.set_current_level(1u);
        assignment.set_current_trail_position(0u);
        assignment.assign(watched_literal, live_ref);

        proof_manager proof_manager;
        proof_manager.on_add_original(live_ref, literals);
        const auto stable_id = proof_manager.stable_id_for_clause(live_ref);

        store::clause_cold cold_store;
        cold_store.set_enabled(true);
        cold_store.demote_to_cold(live_ref, literals);

        garbage_collector collector;
        collector.attach_database(database);
        collector.attach_watch_list(watch_list);
        collector.attach_assignment_store(assignment);
        collector.attach_proof_manager(proof_manager);
        collector.attach_cold_store(cold_store);

        for (std::uint32_t cycle {}; cycle < 5u; ++cycle)
        {
            const auto garbage_ref = database.add_clause(
                std::array<literal, 1> {literal {variable {static_cast<std::uint32_t>(800u + cycle)}, false}}, true);
            database.mark_garbage(garbage_ref);

            collector.collect();
            collector.relocate_live_clause();
            collector.rewrite_watchers();
            collector.rewrite_reasons();
            collector.finalize_cycle();

            live_ref = database.storage_of().resolve_ref(live_ref);
            REQUIRE(live_ref.valid());
            REQUIRE(database.storage_of().proof_id_of(live_ref).valid());
            REQUIRE(proof_manager.stable_id_for_clause(live_ref).equals(stable_id));
            REQUIRE(assignment.reason_of(watched_literal.variable_of()) == live_ref);
            REQUIRE(watch_list.contains(watched_literal, watch {blocking_literal, live_ref}));
            REQUIRE(cold_store.is_cold(live_ref));
            REQUIRE(cold_store.decode_literals(live_ref).size() == literals.size());

            const auto quality = database.quality_of(live_ref);
            REQUIRE(quality.glue == 2u);
            REQUIRE(quality.used_count == 1u);
            REQUIRE(quality.activity == 6.0);
        }

        REQUIRE(collector.relocated_clause_count() == 1u);
    }
}
