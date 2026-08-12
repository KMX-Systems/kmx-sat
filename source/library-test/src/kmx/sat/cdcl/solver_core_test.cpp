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
        std::vector<literal> satisfiable_clause {literal {variable {1}, false}, literal {variable {2}, false}};
        satisfiable_solver.add_problem_clause(std::span<const literal> {satisfiable_clause});
        REQUIRE(satisfiable_solver.solve({}) == solver_core::status::satisfiable);
        REQUIRE(satisfiable_solver.preprocess_run_count() == 1u);
        REQUIRE(!satisfiable_solver.extract_internal_model().empty());

        solver_core assumption_solver;
        assumption_solver.add_problem_clause({literal {variable {1}, false}});
        const std::vector<literal> assumptions {literal {variable {1}, true}};
        REQUIRE(assumption_solver.solve_under_assumptions(std::span<const literal> {assumptions}) == solver_core::status::unsatisfiable);
        REQUIRE(assumption_solver.preprocess_run_count() == 1u);
        REQUIRE(!assumption_solver.extract_failed_core().empty());

        solver_core learning_solver;
        learning_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}});
        learning_solver.add_problem_clause({literal {variable {1}, true}});
        learning_solver.add_problem_clause({literal {variable {2}, true}});
        REQUIRE(learning_solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(learning_solver.preprocess_run_count() == 1u);
        REQUIRE(learning_solver.learned_clause_count() >= 1u);
        REQUIRE(learning_solver.retained_learned_clause_count() >= 1u);
        REQUIRE(learning_solver.transient_state_was_reset());

        solver_core subsumed_solver;
        subsumed_solver.add_problem_clause({literal {variable {4}, false}});
        subsumed_solver.add_problem_clause({literal {variable {4}, false}, literal {variable {5}, false}});
        REQUIRE(subsumed_solver.solve({}) == solver_core::status::satisfiable);
        REQUIRE(subsumed_solver.preprocess_run_count() == 1u);
        REQUIRE(subsumed_solver.subsumed_clause_count() >= 1u);

        solver_core unknown_solver;
        unknown_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}, literal {variable {3}, false}});
        solve_request limited_request {};
        limited_request.decision_limit = 1;
        REQUIRE(unknown_solver.solve(limited_request) == solver_core::status::unknown);
        REQUIRE(unknown_solver.preprocess_run_count() == 1u);

        solver_core option_solver;
        option_solver.persist_option_subset();
        REQUIRE(option_solver.persisted_option_subset());

        solver_core reset_solver;
        proof::tracer::view reset_drat_sink {proof::tracer::drat {}};
        reset_solver.attach_proof_tracer(reset_drat_sink);
        reset_solver.add_problem_clause({literal {variable {31}, false}});
        REQUIRE(reset_solver.proof_enabled());
        REQUIRE(reset_solver.proof_buffered_event_count() == 1u);
        reset_solver.reset();
        REQUIRE_FALSE(reset_solver.proof_enabled());
        REQUIRE(reset_solver.proof_buffered_event_count() == 0u);
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
        const auto gate_summary =
            std::find_if(preprocess_summaries.begin(), preprocess_summaries.end(),
                         [](const simplify::scheduler::preprocess::pass_summary& summary) noexcept { return summary.pass_name == "gate"; });
        REQUIRE(gate_summary != preprocess_summaries.end());
        REQUIRE_FALSE(gate_summary->executed);
        REQUIRE(gate_summary->skipped_by_proof_format);

        const auto congruence_summary = std::find_if(preprocess_summaries.begin(), preprocess_summaries.end(),
                                                     [](const simplify::scheduler::preprocess::pass_summary& summary) noexcept
                                                     { return summary.pass_name == "congruence"; });
        REQUIRE(congruence_summary != preprocess_summaries.end());
        REQUIRE_FALSE(congruence_summary->executed);
        REQUIRE(congruence_summary->skipped_by_proof_format);

        solver_core proof_learning_solver;
        proof_learning_solver.attach_proof_tracer(drat_sink);
        proof_learning_solver.add_problem_clause(
            {literal {variable {21}, false}, literal {variable {22}, false}, literal {variable {23}, false}});
        proof_learning_solver.add_problem_clause({literal {variable {21}, true}});
        proof_learning_solver.add_problem_clause({literal {variable {22}, true}});
        proof_learning_solver.add_problem_clause({literal {variable {23}, true}});
        REQUIRE(proof_learning_solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(proof_learning_solver.proof_buffered_event_count() >= 5u);
        REQUIRE(proof_learning_solver.last_proof_event().kind == proof::event_kind::add_derived);
        const auto& antecedent_ids = proof_learning_solver.last_proof_event().antecedent_ids;
        REQUIRE(antecedent_ids.size() == 3u);
        REQUIRE(antecedent_ids[0].valid());
        REQUIRE(antecedent_ids[1].valid());
        REQUIRE(antecedent_ids[2].valid());
        REQUIRE_FALSE(antecedent_ids[0].equals(antecedent_ids[1]));
        REQUIRE_FALSE(antecedent_ids[1].equals(antecedent_ids[2]));
        REQUIRE_FALSE(antecedent_ids[0].equals(antecedent_ids[2]));

        const auto buffered_events = proof_learning_solver.buffered_proof_events();
        REQUIRE(buffered_events.size() >= 5u);

        proof::clause::id conflict_clause_id {};
        proof::clause::id reason_var22_id {};
        proof::clause::id reason_var23_id {};

        for (const auto& event: buffered_events)
        {
            if (event.kind != proof::event_kind::add_original)
            {
                continue;
            }

            if (event.literals.size() == 3u)
            {
                conflict_clause_id = event.clause_id;
                continue;
            }

            if (event.literals.size() == 1u && event.literals[0] == -22)
            {
                reason_var22_id = event.clause_id;
                continue;
            }

            if (event.literals.size() == 1u && event.literals[0] == -23)
            {
                reason_var23_id = event.clause_id;
                continue;
            }
        }

        REQUIRE(conflict_clause_id.valid());
        REQUIRE(reason_var22_id.valid());
        REQUIRE(reason_var23_id.valid());
        REQUIRE(antecedent_ids[0].equals(conflict_clause_id));
        REQUIRE(antecedent_ids[1].equals(reason_var22_id));
        REQUIRE(antecedent_ids[2].equals(reason_var23_id));

        std::vector<proof::clause::id> earlier_clause_ids {};
        for (const auto& event: buffered_events)
        {
            if (event.kind == proof::event_kind::add_original || event.kind == proof::event_kind::add_derived)
            {
                earlier_clause_ids.push_back(event.clause_id);
            }

            if (event.clause_id.equals(proof_learning_solver.last_proof_event().clause_id))
            {
                break;
            }
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
