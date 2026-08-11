#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include <kmx/sat/cdcl/search_coordinator.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl {

TEST_CASE("search coordinator flow", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::cdcl;

    {
        search_coordinator coordinator;
        solve_request request;
        request.assumptions.push_back(literal {variable {1}, false});
        coordinator.apply_assumptions(request);

        coordinator.stage_conflict(clause::ref_t {7});
        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::unsatisfiable);
        REQUIRE(coordinator.assumption_propagation_call_count() == 1);
        REQUIRE(coordinator.propagation_call_count() == 0);
    }

    {
        search_coordinator coordinator;
        coordinator.apply_assumptions({});

        const std::vector<literal> conflict_clause {
            literal {variable {1}, false},
            literal {variable {2}, true},
            literal {variable {3}, false},
            literal {variable {2}, true}
        };
        coordinator.seed_conflict_clause(std::span<const literal> {conflict_clause});
        coordinator.set_decision_level(variable {1}, 1);
        coordinator.set_decision_level(variable {2}, 4);
        coordinator.set_decision_level(variable {3}, 2);
        coordinator.stage_conflict(clause::ref_t {9});
        coordinator.set_next_decision_variable(10);

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
        REQUIRE(coordinator.learned_clause_count() == 1);
        REQUIRE(coordinator.last_asserting_literal().raw() == conflict_clause.front().raw());
        REQUIRE(coordinator.assumption_propagation_call_count() == 1);
        REQUIRE(coordinator.propagation_call_count() == 2);
        REQUIRE(coordinator.conflict_event_count() == 1);
        REQUIRE(coordinator.decision_event_count() == 1);
    }

    {
        search_coordinator coordinator;
        coordinator.apply_assumptions({});
        coordinator.request_restart();
        coordinator.set_next_decision_variable(4);

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
        REQUIRE(coordinator.restart_count() == 1);
        REQUIRE(coordinator.decision_event_count() == 1);
    }

    {
        search_coordinator coordinator;
        coordinator.apply_assumptions({});
        coordinator.request_reduce();
        coordinator.set_next_decision_variable(10);

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
        REQUIRE(coordinator.reduction_pass_count() == 1);
        REQUIRE(coordinator.decision_event_count() == 1);
    }

    {
        search_coordinator coordinator;
        coordinator.apply_assumptions({});
        coordinator.set_restart_interval(2);

        const std::vector<literal> conflict_clause {
            literal {variable {11}, false},
            literal {variable {12}, true}
        };
        coordinator.seed_conflict_clause(std::span<const literal> {conflict_clause});
        coordinator.set_decision_level(variable {11}, 1);
        coordinator.set_decision_level(variable {12}, 2);
        coordinator.stage_conflict(clause::ref_t {13});
        coordinator.set_next_decision_variable(14);

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
        REQUIRE(coordinator.conflict_event_count() == 1);
        REQUIRE(coordinator.restart_count() == 0);
        REQUIRE(coordinator.current_restart_budget() == 1);
    }

    {
        search_coordinator coordinator;
        coordinator.apply_assumptions({});
        coordinator.set_restart_interval(1);

        const std::vector<literal> conflict_clause {
            literal {variable {15}, false},
            literal {variable {16}, true}
        };
        coordinator.seed_conflict_clause(std::span<const literal> {conflict_clause});
        coordinator.set_decision_level(variable {15}, 1);
        coordinator.set_decision_level(variable {16}, 2);
        coordinator.stage_conflict(clause::ref_t {17});
        coordinator.set_next_decision_variable(18);

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
        REQUIRE(coordinator.conflict_event_count() == 1);
        REQUIRE(coordinator.restart_count() == 1);
        REQUIRE(coordinator.current_restart_budget() == 1);
    }

    {
        search_coordinator coordinator;
        solve_request request;
        request.decision_limit = 1;
        coordinator.apply_assumptions(request);
        coordinator.set_next_decision_variable(8);

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::terminated);
        REQUIRE(coordinator.decision_event_count() == 1);
    }

    {
        search_coordinator coordinator;
        solve_request request;
        request.conflict_limit = 1;
        coordinator.apply_assumptions(request);
        coordinator.stage_conflict(clause::ref_t {12});

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::terminated);
        REQUIRE(coordinator.conflict_event_count() == 1);
    }

    {
        search_coordinator coordinator;
        coordinator.apply_assumptions({});

        coordinator.run_search_epoch();

        REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::satisfiable);
        REQUIRE(coordinator.assumption_propagation_call_count() == 1);
        REQUIRE(coordinator.propagation_call_count() == 1);

        coordinator.run_search_epoch();
        REQUIRE(coordinator.assumption_propagation_call_count() == 1);
        REQUIRE(coordinator.propagation_call_count() == 1);
    }

    // removed std::cout: "search coordinator flow test passed\n";
    }

} // namespace
