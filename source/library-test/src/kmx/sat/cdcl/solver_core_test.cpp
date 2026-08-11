#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include <kmx/sat/cdcl/solver_core.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl {

TEST_CASE("solver core", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::cdcl;

    solver_core solver;
    solver.add_problem_clause({literal {variable {1}, false}});
    solver.add_problem_clause({literal {variable {1}, true}});
    REQUIRE(solver.original_clause_count() == 2);
    REQUIRE(solver.solve({}) == solver_core::status::unsatisfiable);
    REQUIRE(solver.current_search_outcome() == search_coordinator::outcome::unsatisfiable);

    solver_core satisfiable_solver;
    std::vector<literal> satisfiable_clause {literal {variable {1}, false}, literal {variable {2}, false}};
    satisfiable_solver.add_problem_clause(std::span<const literal> {satisfiable_clause});
    REQUIRE(satisfiable_solver.solve({}) == solver_core::status::satisfiable);
    REQUIRE(!satisfiable_solver.extract_internal_model().empty());

    solver_core assumption_solver;
    assumption_solver.add_problem_clause({literal {variable {1}, false}});
    const std::vector<literal> assumptions {literal {variable {1}, true}};
    REQUIRE(assumption_solver.solve_under_assumptions(std::span<const literal> {assumptions}) == solver_core::status::unsatisfiable);
    REQUIRE(!assumption_solver.extract_failed_core().empty());

    solver_core learning_solver;
    learning_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}});
    learning_solver.add_problem_clause({literal {variable {1}, true}});
    learning_solver.add_problem_clause({literal {variable {2}, true}});
    REQUIRE(learning_solver.solve({}) == solver_core::status::unsatisfiable);
    REQUIRE(learning_solver.learned_clause_count() >= 1u);

    solver_core unknown_solver;
    unknown_solver.add_problem_clause({literal {variable {1}, false}, literal {variable {2}, false}, literal {variable {3}, false}});
    solve_request limited_request {};
    limited_request.decision_limit = 1;
    REQUIRE(unknown_solver.solve(limited_request) == solver_core::status::unknown);

    // removed std::cout: "solver core test passed\n";
    }

} // namespace
