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
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("live proof identity survives repeated movement", "[sat]")
    {
        proof::tracer::drat concrete_sink;
        proof::tracer::view sink {concrete_sink};
        solver solver;
        solver.attach_proof_sink(sink);
        solver.add_clause(std::array<literal, 1> {literal {variable {1u}, false}});
        solver.assume(literal {variable {1u}, true});
        const auto result = solver.solve(solve_request {});
        REQUIRE(result.status_of() == solve_result::status::unsatisfiable);
        REQUIRE(result.proof_summary_of().proof_enabled);
        REQUIRE(result.proof_summary_of().proof_checked);
        const auto before_events = solver.buffered_proof_events();
        REQUIRE(!before_events.empty());
        REQUIRE(before_events.back().kind == proof::event_kind::conclusion);

        cdcl::variable_mapper mapper;
        const auto internal_a = mapper.ensure_external_variable(variable {91u});
        const auto internal_b = mapper.ensure_external_variable(variable {92u});
        mapper.mark_eliminated(internal_b);
        cdcl::clause::database database;
        const std::array<literal, 2> literals {literal {internal_a, false}, literal {internal_a, true}};
        auto ref = database.add_clause(literals, true);
        cdcl::bank::watch_list watches;
        watches.watch_literal(literals[0], cdcl::watch {literals[1], ref});
        cdcl::store::assignment assignment;
        assignment.assign(literals[0], ref);
        proof_manager proof_manager;
        proof_manager.on_add_original(ref, literals);
        const auto stable_id = proof_manager.stable_id_for_clause(ref);
        cdcl::store::clause_cold cold;
        cold.set_enabled(true);
        cold.demote_to_cold(ref, literals);
        cdcl::garbage_collector collector;
        collector.attach_database(database);
        collector.attach_watch_list(watches);
        collector.attach_assignment_store(assignment);
        collector.attach_proof_manager(proof_manager);
        collector.attach_cold_store(cold);
        cdcl::compaction_service compactor;
        compactor.attach_mapper(mapper);
        compactor.attach_database(database);
        compactor.attach_watch_list(watches);
        compactor.attach_assignment_store(assignment);
        compactor.attach_proof_manager(proof_manager);
        compactor.attach_cold_store(cold);

        for (std::uint32_t cycle {}; cycle < 8u; ++cycle)
        {
            const auto garbage = database.add_clause(std::array<literal, 1> {literal {variable {200u + cycle}, false}}, true);
            database.mark_garbage(garbage);
            collector.collect();
            collector.relocate_live_clause();
            collector.rewrite_watchers();
            collector.rewrite_reasons();
            collector.finalize_cycle();
            ref = database.storage_of().resolve_ref(ref);
            compactor.build_variable_permutation();
            compactor.rewrite_literals();
            compactor.rewrite_watches();
            compactor.rewrite_reasons();
            compactor.rewrite_external_mapping();
            ref = database.storage_of().resolve_ref(ref);
            REQUIRE(proof_manager.stable_id_for_clause(ref).equals(stable_id));
            REQUIRE(cold.is_cold(ref));
            REQUIRE(cold.decode_literals(ref).size() == 2u);
        }

        REQUIRE(proof_manager.validate_checkers());
        REQUIRE(proof_manager.buffered_event_count() > 0u);
    }
}
