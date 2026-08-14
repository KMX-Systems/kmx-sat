#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <span>
#include <string>
#include <vector>

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
    TEST_CASE("live learned proof provenance and movement share stable identities", "[sat]")
    {
        proof::tracer::drat concrete_sink;
        proof::tracer::view sink {concrete_sink};
        solver solver;
        solver.attach_proof_sink(sink);
        const std::array<std::array<literal, 3>, 8> clauses {
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, true}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, true}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, false}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, true}},
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, true}, literal {variable {3u}, false}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, true}, literal {variable {3u}, false}},
            std::array<literal, 3> {literal {variable {1u}, true}, literal {variable {2u}, false}, literal {variable {3u}, false}},
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, false}},
        };
        for (const auto& clause: clauses)
            solver.add_clause(std::span<const literal> {clause});

        const auto result = solver.solve(solve_request {});
        REQUIRE(result.status_of() == solve_result::status::unsatisfiable);
        REQUIRE(result.proof_summary_of().proof_checked);

        const auto events = solver.buffered_proof_events();
        REQUIRE(!events.empty());
        REQUIRE(events.back().kind == proof::event_kind::conclusion);
        std::size_t derived_count = 0u;
        for (std::size_t index {}; index < events.size(); ++index)
        {
            const auto& event = events[index];
            if (event.kind != proof::event_kind::add_derived)
                continue;
            ++derived_count;
            REQUIRE(!event.antecedent_ids.empty());
            for (const auto antecedent: event.antecedent_ids)
            {
                bool appeared_earlier = false;
                for (std::size_t prior {}; prior < index; ++prior)
                {
                    if ((events[prior].kind == proof::event_kind::add_original || events[prior].kind == proof::event_kind::add_derived) &&
                        events[prior].clause_id.equals(antecedent))
                    {
                        appeared_earlier = true;
                        break;
                    }
                }
                REQUIRE(appeared_earlier);
            }
        }
        REQUIRE(derived_count > 0u);

        const auto original = std::find_if(events.begin(), events.end(), [](const proof::proof_event& event) noexcept
                                           { return event.kind == proof::event_kind::add_original; });
        REQUIRE(original != events.end());

        proof_manager movement_manager;
        movement_manager.on_add_original(original->clause_ref, {});
        const auto stable_id = movement_manager.stable_id_for_clause(original->clause_ref);
        const cdcl::clause::ref_t relocated_ref {original->clause_ref.offset() + 1000u};
        movement_manager.on_clause_relocated(original->clause_ref, relocated_ref);
        REQUIRE(movement_manager.stable_id_for_clause(relocated_ref).equals(stable_id));
        movement_manager.on_shrink_clause(relocated_ref, {});
        movement_manager.on_delete_clause(relocated_ref);
        REQUIRE(!movement_manager.stable_id_for_clause(relocated_ref).valid());
        REQUIRE(movement_manager.validate_checkers());

        if (const char* report_path = std::getenv("KMX_SAT_PROOF_REPORT"); report_path != nullptr && *report_path != '\0')
        {
            std::size_t original_count = 0u;
            std::size_t derived_count_reported = 0u;
            std::size_t conclusion_count = 0u;
            std::size_t antecedent_count = 0u;
            for (const auto& event: events)
            {
                original_count += event.kind == proof::event_kind::add_original ? 1u : 0u;
                derived_count_reported += event.kind == proof::event_kind::add_derived ? 1u : 0u;
                conclusion_count += event.kind == proof::event_kind::conclusion ? 1u : 0u;
                antecedent_count += event.antecedent_ids.size();
            }
            std::ofstream report {report_path, std::ios::out | std::ios::trunc};
            report << "{\"schema\":1,\"status\":\""
                   << (result.status_of() == solve_result::status::unsatisfiable ? "UNSATISFIABLE" : "OTHER")
                   << "\",\"proof_checked\":" << (result.proof_summary_of().proof_checked ? "true" : "false")
                   << ",\"original_events\":" << original_count << ",\"derived_events\":" << derived_count_reported
                   << ",\"conclusion_events\":" << conclusion_count << ",\"antecedent_ids\":" << antecedent_count
                   << ",\"movement_identity_preserved\":true,\"deleted_identity_retired\":true}\n";
        }
    }

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
