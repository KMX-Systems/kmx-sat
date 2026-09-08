#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>

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
    static std::size_t resident_set_kb() noexcept
    {
        std::ifstream status {"/proc/self/status"};
        std::string name;
        std::size_t value {};
        std::string unit;
        while (status >> name >> value >> unit)
            if (name == "VmRSS:")
                return value;
        return 0u;
    }

    TEST_CASE("combined relocation and compaction keeps memory bounded", "[sat]")
    {
        variable_mapper mapper;
        const auto external_a = variable {1401u};
        const auto external_b = variable {1402u};
        const auto external_c = variable {1403u};
        const auto internal_a = mapper.ensure_external_variable(external_a);
        const auto internal_b = mapper.ensure_external_variable(external_b);
        const auto internal_c = mapper.ensure_external_variable(external_c);
        mapper.mark_eliminated(internal_b);

        clause::database database;
        const std::array<literal, 2u> literals {literal {internal_a, false}, literal {internal_c, true}};
        auto clause_ref = database.add_clause(literals, true);
        database.set_glue(clause_ref, 2u);
        database.increment_used_count(clause_ref);
        database.increment_activity(clause_ref, 3.0);

        bank::watch_list watches;
        watches.watch_literal(literals[0u], watch {literals[1u], clause_ref});
        store::assignment assignment;
        assignment.set_current_level(1u);
        assignment.assign(literals[0u], clause_ref);
        proof_manager proof;
        proof.on_add_original(clause_ref, literals);
        const auto stable_id = proof.stable_id_for_clause(clause_ref);
        store::clause_cold cold;
        cold.set_enabled(true);
        cold.demote_to_cold(clause_ref, literals);

        garbage_collector collector;
        collector.attach_database(database);
        collector.attach_watch_list(watches);
        collector.attach_assignment_store(assignment);
        collector.attach_proof_manager(proof);
        collector.attach_cold_store(cold);
        compaction_service compactor;
        compactor.attach_mapper(mapper);
        compactor.attach_database(database);
        compactor.attach_watch_list(watches);
        compactor.attach_assignment_store(assignment);
        compactor.attach_proof_manager(proof);
        compactor.attach_cold_store(cold);

        const auto initial_rss_kb = resident_set_kb();
        auto peak_rss_kb = initial_rss_kb;
        for (std::uint32_t cycle {}; cycle < 256u; ++cycle)
        {
            const auto garbage_ref =
                database.add_clause(std::array<literal, 1u> {literal {variable {static_cast<std::uint32_t>(1500u + cycle)}, false}}, true);
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
            REQUIRE(clause_ref.valid());
            REQUIRE(proof.stable_id_for_clause(clause_ref).equals(stable_id));
            REQUIRE(cold.is_cold(clause_ref));
            REQUIRE(cold.decode_literals(clause_ref).size() == 2u);
            peak_rss_kb = std::max(peak_rss_kb, resident_set_kb());
        }

        REQUIRE(peak_rss_kb >= initial_rss_kb);
        REQUIRE(peak_rss_kb <= initial_rss_kb + 64u * 1024u);
    }
}
