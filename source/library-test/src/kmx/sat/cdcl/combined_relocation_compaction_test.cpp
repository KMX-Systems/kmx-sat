#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/compaction_service.hpp>
#include <kmx/sat/cdcl/garbage_collector.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("combined relocation and compaction preserves logical clause state", "[sat]")
    {
        variable_mapper mapper;
        const variable external_a {1201u};
        const variable external_b {1202u};
        const variable external_c {1203u};
        const auto internal_a = mapper.ensure_external_variable(external_a);
        const auto internal_b = mapper.ensure_external_variable(external_b);
        const auto internal_c = mapper.ensure_external_variable(external_c);
        mapper.mark_eliminated(internal_b);

        clause::database database;
        const std::array<literal, 2> original_literals {literal {internal_a, false}, literal {internal_c, true}};
        auto clause_ref = database.add_clause(original_literals, true);
        database.set_glue(clause_ref, 2u);
        database.increment_used_count(clause_ref);
        database.increment_activity(clause_ref, 9.0);

        bank::watch_list watch_list;
        const auto watched_literal = original_literals[0];
        const auto blocking_literal = original_literals[1];
        watch_list.watch_literal(watched_literal, watch {blocking_literal, clause_ref});

        store::assignment assignment;
        assignment.set_current_level(1u);
        assignment.set_current_trail_position(0u);
        assignment.assign(watched_literal, clause_ref);

        proof_manager proof_manager;
        proof_manager.on_add_original(clause_ref, original_literals);
        const auto stable_id = proof_manager.stable_id_for_clause(clause_ref);

        store::clause_cold cold_store;
        cold_store.set_enabled(true);
        cold_store.demote_to_cold(clause_ref, original_literals);

        garbage_collector collector;
        collector.attach_database(database);
        collector.attach_watch_list(watch_list);
        collector.attach_assignment_store(assignment);
        collector.attach_proof_manager(proof_manager);
        collector.attach_cold_store(cold_store);

        compaction_service compactor;
        compactor.attach_mapper(mapper);
        compactor.attach_database(database);
        compactor.attach_watch_list(watch_list);
        compactor.attach_assignment_store(assignment);
        compactor.attach_proof_manager(proof_manager);
        compactor.attach_cold_store(cold_store);

        for (std::uint32_t cycle {}; cycle < 64u; ++cycle)
        {
            const auto garbage_ref =
                database.add_clause(std::array<literal, 1> {literal {variable {static_cast<std::uint32_t>(1300u + cycle)}, false}}, true);
            database.mark_garbage(garbage_ref);

            collector.collect();
            collector.relocate_live_clause();
            collector.rewrite_watchers();
            collector.rewrite_reasons();
            collector.finalize_cycle();
            clause_ref = database.storage_of().resolve_ref(clause_ref);

            compactor.build_variable_permutation();
            compactor.rewrite_literals();
            compactor.rewrite_watches();
            compactor.rewrite_reasons();
            compactor.rewrite_external_mapping();
            clause_ref = database.storage_of().resolve_ref(clause_ref);

            const auto compacted_a = mapper.to_internal_literal(literal {external_a, false});
            const auto compacted_c = mapper.to_internal_literal(literal {external_c, true});
            REQUIRE(clause_ref.valid());
            REQUIRE(database.storage_of().proof_id_of(clause_ref).valid());
            REQUIRE(proof_manager.stable_id_for_clause(clause_ref).equals(stable_id));
            REQUIRE(assignment.reason_of(compacted_a.variable_of()) == clause_ref);
            REQUIRE(watch_list.contains(compacted_a, watch {compacted_c, clause_ref}));
            REQUIRE(cold_store.is_cold(clause_ref));
            REQUIRE(cold_store.decode_literals(clause_ref).size() == 2u);

            const auto quality = database.quality_of(clause_ref);
            REQUIRE(quality.glue == 2u);
            REQUIRE(quality.used_count == 1u);
            REQUIRE(quality.activity == 9.0);
        }
    }
}
