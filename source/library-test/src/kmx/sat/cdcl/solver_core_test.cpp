#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include <kmx/sat/cdcl/solver_core.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{

    TEST_CASE("solver core", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::cdcl;

        solver_core solver;
        solver.add_problem_clause({literal {variable {1}, false}});
        solver.add_problem_clause({literal {variable {1}, true}});
        REQUIRE(solver.original_clause_count() == 2);
        REQUIRE(solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(solver.preprocess_run_count() == 1u);
        REQUIRE(solver.current_search_outcome() == search_coordinator::outcome::unsatisfiable);

        solver_core satisfiable_solver;
        std::vector<literal> satisfiable_clause {literal {variable {1}, true}, literal {variable {2}, true}};
        satisfiable_solver.add_problem_clause(std::span<const literal> {satisfiable_clause});
        REQUIRE(satisfiable_solver.solve({}) == solver_core::status::satisfiable);
        REQUIRE(satisfiable_solver.preprocess_run_count() == 1u);
        REQUIRE(!satisfiable_solver.extract_internal_model().empty());
        REQUIRE(satisfiable_solver.propagation_assignment_count() > 0u);
        REQUIRE(satisfiable_solver.watch_entry_scan_count() > 0u);
        REQUIRE(satisfiable_solver.watch_partition_iterate_count() > 0u);

        solver_core binary_chain_solver;
        binary_chain_solver.add_problem_clause({literal {variable {70u}, false}});
        for (std::uint32_t variable_index {70u}; variable_index < 80u; ++variable_index)
        {
            binary_chain_solver.add_problem_clause(
                {literal {variable {variable_index}, true}, literal {variable {variable_index + 1u}, false}});
        }
        REQUIRE(binary_chain_solver.solve({}) == solver_core::status::satisfiable);
        REQUIRE(binary_chain_solver.binary_watch_scan_count() >= 10u);
        REQUIRE(binary_chain_solver.binary_watch_conflict_count() == 0u);

        solver_core binary_conflict_solver;
        binary_conflict_solver.add_problem_clause({literal {variable {90u}, false}});
        binary_conflict_solver.add_problem_clause({literal {variable {91u}, false}});
        binary_conflict_solver.add_problem_clause({literal {variable {90u}, true}, literal {variable {91u}, true}});
        REQUIRE(binary_conflict_solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(binary_conflict_solver.binary_watch_scan_count() > 0u);
        REQUIRE(binary_conflict_solver.binary_watch_conflict_count() > 0u);

        solver_core assumption_solver;
        assumption_solver.add_problem_clause({literal {variable {1}, false}});
        const std::vector<literal> assumptions {literal {variable {1}, true}};
        REQUIRE(assumption_solver.solve_under_assumptions(std::span<const literal> {assumptions}) == solver_core::status::unsatisfiable);
        REQUIRE(assumption_solver.preprocess_run_count() == 1u);
        REQUIRE(!assumption_solver.extract_failed_core().empty());
        REQUIRE(assumption_solver.extract_failed_core().size() == 1u);
        REQUIRE(assumption_solver.extract_failed_core().front().raw() == assumptions.front().raw());
        REQUIRE(assumption_solver.propagator_assumption_call_count() == 1u);
        REQUIRE(assumption_solver.propagator_call_count() >= 1u);

        solver_core assumption_subset_solver;
        assumption_subset_solver.add_problem_clause({literal {variable {1}, false}});
        const std::vector<literal> subset_assumptions {literal {variable {1}, true}, literal {variable {9}, false}};
        REQUIRE(assumption_subset_solver.solve_under_assumptions(std::span<const literal> {subset_assumptions}) ==
                solver_core::status::unsatisfiable);
        REQUIRE(assumption_subset_solver.extract_failed_core().size() == 1u);
        REQUIRE(assumption_subset_solver.extract_failed_core().front().raw() == subset_assumptions.front().raw());
        REQUIRE(assumption_subset_solver.propagator_assumption_call_count() == 1u);

        solver_core assumption_reason_solver;
        assumption_reason_solver.add_problem_clause({literal {variable {1}, true}, literal {variable {2}, false}});
        assumption_reason_solver.add_problem_clause({literal {variable {2}, true}});
        const std::vector<literal> reason_assumptions {literal {variable {1}, false}, literal {variable {9}, false}};
        REQUIRE(assumption_reason_solver.solve_under_assumptions(std::span<const literal> {reason_assumptions}) ==
                solver_core::status::unsatisfiable);
        REQUIRE(assumption_reason_solver.extract_failed_core().size() == 1u);
        REQUIRE(assumption_reason_solver.extract_failed_core().front().raw() == reason_assumptions.front().raw());

        solver_core propagation_guided_solver;
        propagation_guided_solver.add_problem_clause({literal {variable {1}, false}});
        propagation_guided_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}});
        solve_request propagation_guided_request {};
        propagation_guided_request.decision_limit = 2;
        REQUIRE(propagation_guided_solver.solve(propagation_guided_request) == solver_core::status::satisfiable);

        solver_core learning_solver;
        // v1 has no unit clause, so it must be branched on; whichever polarity is tried first, propagation
        // forces a direct contradiction on v2, exercising real first-UIP resolution at a genuine decision level.
        learning_solver.add_problem_clause({literal {variable {1}, true}, literal {variable {2}, false}});
        learning_solver.add_problem_clause({literal {variable {1}, true}, literal {variable {2}, true}});
        learning_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}});
        learning_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, true}});
        REQUIRE(learning_solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(learning_solver.preprocess_run_count() == 1u);
        REQUIRE(learning_solver.learned_clause_count() >= 1u);
        // The learned clause here is a single asserting unit literal (the decision variable's negation), so the
        // minimizer correctly skips it (minimization/shrink only apply to clauses with more than one literal).
        REQUIRE(learning_solver.minimized_learned_clause_count() == 0u);
        REQUIRE(learning_solver.shrunk_learned_clause_count() == 0u);
        REQUIRE(learning_solver.learned_clause_shrink_event_count() == 0u);
        REQUIRE(learning_solver.retained_learned_clause_count() >= 1u);
        REQUIRE(learning_solver.transient_state_was_reset());

        solver_core tautology_guard_solver;
        // Clause 1 is an original tautology (contains v11 and ~v11): it is permanently satisfied and can never
        // itself be the falsified clause in a conflict. The genuine (and only) source of unsatisfiability here is
        // the direct level-0 contradiction between the two unit clauses on v11; this verifies that an inert
        // tautological original clause coexisting with a real contradiction does not confuse conflict handling.
        tautology_guard_solver.add_problem_clause(
            {literal {variable {11}, false}, literal {variable {12}, true}, literal {variable {11}, true}});
        tautology_guard_solver.add_problem_clause({literal {variable {11}, false}});
        tautology_guard_solver.add_problem_clause({literal {variable {11}, true}});
        tautology_guard_solver.add_problem_clause({literal {variable {12}, false}});
        REQUIRE(tautology_guard_solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(tautology_guard_solver.learned_clause_count() == 0u);
        REQUIRE(tautology_guard_solver.minimized_learned_clause_count() == 0u);
        REQUIRE(tautology_guard_solver.shrunk_learned_clause_count() == 0u);

        solver_core subsumed_solver;
        subsumed_solver.add_problem_clause({literal {variable {4}, false}});
        subsumed_solver.add_problem_clause({literal {variable {4}, false}, literal {variable {5}, false}});
        REQUIRE(subsumed_solver.solve({}) == solver_core::status::satisfiable);
        REQUIRE(subsumed_solver.preprocess_run_count() == 1u);
        REQUIRE(subsumed_solver.subsumed_clause_count() >= 1u);
        const auto inprocess_snapshot = subsumed_solver.current_inprocess_telemetry_snapshot();
        REQUIRE(inprocess_snapshot.conflict_density_ema >= 0.0);
        REQUIRE(inprocess_snapshot.structural_gain_ema >= 0.0);
        REQUIRE(inprocess_snapshot.restart_pressure_ema >= 0.0);
        REQUIRE(inprocess_snapshot.reduction_pressure_ema >= 0.0);
        REQUIRE(inprocess_snapshot.learned_clause_pressure_ema >= 0.0);

        const auto& preprocess_plan = subsumed_solver.preprocess_current_pass_plan();
        if (preprocess_plan.skip_memory_heavy_from_learned_clause_pressure)
        {
            REQUIRE(preprocess_plan.skip_memory_heavy_from_inprocess_pressure);
            REQUIRE(preprocess_plan.skip_memory_heavy_passes);
        }

        solver_core unknown_solver;
        unknown_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}, literal {variable {3}, false}});
        solve_request limited_request {};
        limited_request.decision_limit = 1;
        REQUIRE(unknown_solver.solve(limited_request) == solver_core::status::unknown);
        REQUIRE(unknown_solver.current_search_outcome() == search_coordinator::outcome::terminated);
        REQUIRE(unknown_solver.current_search_termination_cause() == search_coordinator::termination_cause::decision_limit);
        REQUIRE(unknown_solver.preprocess_run_count() == 1u);

        solver_core unlimited_unsat_solver;
        unlimited_unsat_solver.add_problem_clause({literal {variable {1}, false}});
        unlimited_unsat_solver.add_problem_clause({literal {variable {1}, true}});
        solve_request unlimited_request {};
        unlimited_request.conflict_limit = 0u;
        unlimited_request.decision_limit = 0u;
        REQUIRE(unlimited_unsat_solver.solve(unlimited_request) == solver_core::status::unsatisfiable);
        REQUIRE(unlimited_unsat_solver.current_search_outcome() == search_coordinator::outcome::unsatisfiable);

        solver_core unlimited_sat_solver;
        std::vector<literal> unlimited_sat_clause {literal {variable {1}, false}, literal {variable {2}, false}};
        unlimited_sat_solver.add_problem_clause(std::span<const literal> {unlimited_sat_clause});
        REQUIRE(unlimited_sat_solver.solve(unlimited_request) == solver_core::status::satisfiable);
        REQUIRE(unlimited_sat_solver.current_search_outcome() == search_coordinator::outcome::satisfiable);

        solver_core conflict_limited_solver;
        conflict_limited_solver.add_problem_clause({literal {variable {1}, false}});
        conflict_limited_solver.add_problem_clause({literal {variable {1}, true}});
        solve_request conflict_limited_request {};
        conflict_limited_request.conflict_limit = 1u;
        REQUIRE(conflict_limited_solver.solve(conflict_limited_request) == solver_core::status::unknown);
        REQUIRE(conflict_limited_solver.current_search_outcome() == search_coordinator::outcome::terminated);
        REQUIRE(conflict_limited_solver.current_search_termination_cause() == search_coordinator::termination_cause::conflict_limit);

        solver_core tautology_conflict_limited_solver;
        // Same construction as tautology_guard_solver above: clause 1 is an inert original tautology, and the
        // genuine, immediate conflict comes from the direct level-0 contradiction between the two v11 unit clauses.
        tautology_conflict_limited_solver.add_problem_clause(
            {literal {variable {11}, false}, literal {variable {12}, true}, literal {variable {11}, true}});
        tautology_conflict_limited_solver.add_problem_clause({literal {variable {11}, false}});
        tautology_conflict_limited_solver.add_problem_clause({literal {variable {11}, true}});
        tautology_conflict_limited_solver.add_problem_clause({literal {variable {12}, false}});
        solve_request tautology_conflict_limited_request {};
        tautology_conflict_limited_request.conflict_limit = 1u;
        REQUIRE(tautology_conflict_limited_solver.solve(tautology_conflict_limited_request) == solver_core::status::unknown);
        REQUIRE(tautology_conflict_limited_solver.current_search_outcome() == search_coordinator::outcome::terminated);
        REQUIRE(tautology_conflict_limited_solver.current_search_termination_cause() ==
                search_coordinator::termination_cause::conflict_limit);
        REQUIRE(tautology_conflict_limited_solver.learned_clause_count() == 0u);

        solver_core option_solver;
        option_solver.persist_option_subset();
        REQUIRE(option_solver.persisted_option_subset());
        REQUIRE_FALSE(option_solver.chb_enabled());
        option_solver.set_chb_enabled(true);
        REQUIRE(option_solver.chb_enabled());
        option_solver.set_cold_storage_enabled(true);
        REQUIRE(option_solver.cold_storage_enabled());
        REQUIRE(option_solver.cold_footprint_bytes() == 0u);
        option_solver.set_decision_maintenance_intervals(1u, 1u, 1u);
        const auto option_intervals = option_solver.decision_maintenance_intervals();
        REQUIRE(option_intervals[0] == 1u);
        REQUIRE(option_intervals[1] == 1u);
        REQUIRE(option_intervals[2] == 1u);

        solver_core maintenance_solver;
        maintenance_solver.set_decision_maintenance_intervals(1u, 1u, 1u);
        maintenance_solver.add_problem_clause({literal {variable {41}, false}, literal {variable {42}, false}});
        maintenance_solver.add_problem_clause({literal {variable {41}, true}});
        maintenance_solver.add_problem_clause({literal {variable {42}, true}});
        REQUIRE(maintenance_solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(maintenance_solver.decision_evsids_rescale_count() >= 1u);
        REQUIRE(maintenance_solver.decision_chb_decay_count() >= 1u);

        solver_core reset_solver;
        proof::tracer::view reset_drat_sink {proof::tracer::drat {}};
        reset_solver.attach_proof_tracer(reset_drat_sink);
        reset_solver.add_problem_clause({literal {variable {31}, false}});
        REQUIRE(reset_solver.proof_enabled());
        REQUIRE(reset_solver.proof_buffered_event_count() == 1u);
        REQUIRE(reset_solver.propagator_call_count() == 0u);
        REQUIRE(reset_solver.propagator_assumption_call_count() == 0u);
        reset_solver.reset();
        REQUIRE_FALSE(reset_solver.proof_enabled());
        REQUIRE(reset_solver.proof_buffered_event_count() == 0u);
        REQUIRE(reset_solver.propagator_call_count() == 0u);
        REQUIRE(reset_solver.propagator_assumption_call_count() == 0u);
        reset_solver.attach_proof_tracer(reset_drat_sink);
        reset_solver.add_problem_clause({literal {variable {32}, false}});
        REQUIRE(reset_solver.proof_enabled());
        REQUIRE(reset_solver.proof_buffered_event_count() == 1u);
        REQUIRE(reset_solver.last_proof_event().kind == proof::event_kind::add_original);

        solver_core proof_gated_solver;
        proof::tracer::view drat_sink {proof::tracer::drat {}};
        proof_gated_solver.attach_proof_tracer(drat_sink);
        proof_gated_solver.add_problem_clause({literal {variable {9}, false}});
        REQUIRE(proof_gated_solver.proof_buffered_event_count() == 1u);
        REQUIRE(proof_gated_solver.last_proof_event().kind == proof::event_kind::add_original);
        REQUIRE(proof_gated_solver.solve({}) == solver_core::status::satisfiable);
        REQUIRE(proof_gated_solver.proof_enabled());
        REQUIRE(proof_gated_solver.proof_checkers_valid());

        const auto& preprocess_summaries = proof_gated_solver.preprocess_last_reported_summaries();
        const auto gate_summary = std::find_if(preprocess_summaries.begin(), preprocess_summaries.end(),
                                               [](const simplify::scheduler::preprocess::pass_summary& summary) noexcept
                                               { return summary.id == simplify::scheduler::preprocess::pass_id::gate; });
        REQUIRE(gate_summary != preprocess_summaries.end());
        REQUIRE_FALSE(gate_summary->executed);
        REQUIRE(gate_summary->skipped_by_proof_format);

        const auto congruence_summary = std::find_if(preprocess_summaries.begin(), preprocess_summaries.end(),
                                                     [](const simplify::scheduler::preprocess::pass_summary& summary) noexcept
                                                     { return summary.id == simplify::scheduler::preprocess::pass_id::congruence; });
        REQUIRE(congruence_summary != preprocess_summaries.end());
        REQUIRE_FALSE(congruence_summary->executed);
        REQUIRE(congruence_summary->skipped_by_proof_format);

        solver_core proof_learning_solver;
        proof_learning_solver.attach_proof_tracer(drat_sink);
        // Deterministic two-decision-level construction: v1 forces v2 and v5 true (via a doubled clause pair
        // each, so either polarity of v1 works); v3 forces v4 true likewise; then D conflicts on v2/v4/v5, all
        // necessarily true. The exact multi-hop first-UIP resolution shape (3 distinct antecedents: the conflict
        // clause plus one reason clause each for v2 and v5) is precisely covered, deterministically and without
        // any full-search branch-order sensitivity, by the dedicated analyze_via_resolution tests in
        // cdcl_component_state_test.cpp; here we only assert the always-true, end-to-end proof-integration
        // properties that hold regardless of which explored branch produces the search's terminal conflict.
        proof_learning_solver.add_problem_clause({literal {variable {1}, true}, literal {variable {2}, false}});  // A1 = (~v1 v v2)
        proof_learning_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}}); // A2 = (v1 v v2)
        proof_learning_solver.add_problem_clause({literal {variable {1}, true}, literal {variable {5}, false}});  // E1 = (~v1 v v5)
        proof_learning_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {5}, false}}); // E2 = (v1 v v5)
        proof_learning_solver.add_problem_clause({literal {variable {3}, true}, literal {variable {4}, false}});  // B1 = (~v3 v v4)
        proof_learning_solver.add_problem_clause({literal {variable {3}, false}, literal {variable {4}, false}}); // B2 = (v3 v v4)
        proof_learning_solver.add_problem_clause(
            {literal {variable {2}, true}, literal {variable {4}, true}, literal {variable {5}, true}}); // D = (~v2 v ~v4 v ~v5)
        REQUIRE(proof_learning_solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(proof_learning_solver.proof_buffered_event_count() >= 8u);
        REQUIRE(proof_learning_solver.last_proof_event().kind == proof::event_kind::add_derived);

        const auto& antecedent_ids = proof_learning_solver.last_proof_event().antecedent_ids;
        REQUIRE_FALSE(antecedent_ids.empty());
        for (const auto antecedent_id: antecedent_ids)
            REQUIRE(antecedent_id.valid());
        for (std::size_t index = 0u; index < antecedent_ids.size(); ++index)
            for (std::size_t other = index + 1u; other < antecedent_ids.size(); ++other)
                REQUIRE_FALSE(antecedent_ids[index].equals(antecedent_ids[other]));

        const auto buffered_events = proof_learning_solver.buffered_proof_events();
        REQUIRE(buffered_events.size() >= 8u);

        std::vector<proof::clause::id> earlier_clause_ids {};
        for (const auto& event: buffered_events)
        {
            if (event.kind == proof::event_kind::add_original || event.kind == proof::event_kind::add_derived)
                earlier_clause_ids.push_back(event.clause_id);

            if (event.clause_id.equals(proof_learning_solver.last_proof_event().clause_id))
                break;
        }

        REQUIRE(earlier_clause_ids.size() >= 4u);
        REQUIRE(earlier_clause_ids.back().equals(proof_learning_solver.last_proof_event().clause_id));
        earlier_clause_ids.pop_back();
        REQUIRE(earlier_clause_ids.size() >= 3u);

        for (const auto antecedent_id: antecedent_ids)
        {
            bool found = false;
            for (const auto clause_id: earlier_clause_ids)
            {
                if (clause_id.equals(antecedent_id))
                {
                    found = true;
                    break;
                }
            }
            REQUIRE(found);
        }

        REQUIRE(proof_learning_solver.proof_checkers_valid());

        // removed std::cout: "solver core test passed\n";
    }

} // namespace
