#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/compaction_service.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("repeated variable compaction preserves all clause state", "[sat]")
    {
        variable_mapper mapper;
        const auto external_a = variable {901u};
        const auto external_b = variable {902u};
        const auto external_c = variable {903u};
        const auto internal_a = mapper.ensure_external_variable(external_a);
        const auto internal_b = mapper.ensure_external_variable(external_b);
        const auto internal_c = mapper.ensure_external_variable(external_c);
        mapper.mark_eliminated(internal_b);

        clause::database database;
        const std::array<literal, 2> original_literals {literal {internal_a, false}, literal {internal_c, true}};
        auto clause_ref = database.add_clause(original_literals, true);
        database.set_glue(clause_ref, 2u);
        database.increment_used_count(clause_ref);
        database.increment_activity(clause_ref, 4.0);

        bank::watch_list watches;
        watches.watch_literal(original_literals[0], watch {original_literals[1], clause_ref});
        store::assignment assignment;
        assignment.set_current_level(1u);
        assignment.set_current_trail_position(0u);
        assignment.assign(original_literals[0], clause_ref);

        proof_manager proof_manager;
        proof_manager.on_add_original(clause_ref, original_literals);
        const auto stable_id = proof_manager.stable_id_for_clause(clause_ref);

        store::clause_cold cold_store;
        cold_store.set_enabled(true);
        cold_store.demote_to_cold(clause_ref, original_literals);

        compaction_service service;
        service.attach_mapper(mapper);
        service.attach_database(database);
        service.attach_watch_list(watches);
        service.attach_assignment_store(assignment);
        service.attach_proof_manager(proof_manager);
        service.attach_cold_store(cold_store);

        for (std::uint32_t cycle {}; cycle < 3u; ++cycle)
        {
            service.build_variable_permutation();
            service.rewrite_literals();
            service.rewrite_watches();
            service.rewrite_reasons();
            service.rewrite_external_mapping();

            clause_ref = database.storage_of().resolve_ref(clause_ref);
            REQUIRE(clause_ref.valid());
            REQUIRE(database.storage_of().proof_id_of(clause_ref).valid());
            REQUIRE(proof_manager.stable_id_for_clause(clause_ref).equals(stable_id));
            REQUIRE(assignment.reason_of(original_literals[0].variable_of()) == clause_ref);
            REQUIRE(cold_store.is_cold(clause_ref));
            REQUIRE(cold_store.decode_literals(clause_ref).size() == 2u);

            const auto quality = database.quality_of(clause_ref);
            REQUIRE(quality.glue == 2u);
            REQUIRE(quality.used_count == 1u);
            REQUIRE(quality.activity == 4.0);
        }
    }
}
