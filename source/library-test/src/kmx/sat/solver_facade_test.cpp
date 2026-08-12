#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include <kmx/sat/c_api_adapter.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/solver_state_machine.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{

    TEST_CASE("solver facade", "[sat]")
    {
        using namespace kmx::sat;

        solver sat_solver;
        std::vector<literal> sat_clause {literal {variable {1}, false}};
        sat_solver.add_clause(std::span<const literal> {sat_clause});

        const auto sat_result = sat_solver.solve(solve_request {});
        REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);

        const auto v1 = sat_solver.value_of(variable {1});
        REQUIRE(v1.has_value());
        REQUIRE(*v1);

        solver unsat_solver;
        std::vector<literal> unit_clause {literal {variable {2}, false}};
        unsat_solver.add_clause(std::span<const literal> {unit_clause});
        unsat_solver.assume(literal {variable {2}, true});

        const auto unsat_result = unsat_solver.solve(solve_request {});
        REQUIRE(unsat_result.status_of() == solve_result::status::unsatisfiable);
        REQUIRE(!unsat_result.failed_core().assumptions().empty());
        REQUIRE(unsat_solver.failed(literal {variable {2}, true}));

        SECTION("solver facade tracks lifecycle state transitions")
        {
            solver lifecycle_solver;
            REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::configuring);

            std::vector<literal> lifecycle_clause {literal {variable {3u}, false}};
            lifecycle_solver.add_clause(std::span<const literal> {lifecycle_clause});
            REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::adding);

            const auto lifecycle_result = lifecycle_solver.solve(solve_request {});
            REQUIRE(lifecycle_result.status_of() == solve_result::status::satisfiable);
            REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::sat);

            lifecycle_solver.reset_session();
            REQUIRE(lifecycle_solver.current_state() == solver_state_machine::state::configuring);
        }

        SECTION("solver facade retains callbacks and reports termination")
        {
            solver callback_solver;
            std::size_t terminate_calls = 0u;
            callback_solver.set_terminate(
                [&]() noexcept
                {
                    ++terminate_calls;
                    return true;
                });

            callback_solver.set_learn([&](std::span<const literal>) noexcept { ++terminate_calls; });

            callback_solver.set_external_propagator([&]() noexcept { ++terminate_calls; });

            std::vector<literal> callback_clause {literal {variable {5u}, false}};
            callback_solver.add_clause(std::span<const literal> {callback_clause});
            const auto callback_result = callback_solver.solve(solve_request {});

            REQUIRE(callback_result.status_of() == solve_result::status::terminated);
            REQUIRE(terminate_calls == 1u);
        }

        SECTION("solver facade releases staged incremental assumptions")
        {
            solver assumption_release_solver;
            std::vector<literal> clause {literal {variable {12u}, false}};
            assumption_release_solver.add_clause(std::span<const literal> {clause});
            assumption_release_solver.assume(literal {variable {12u}, true});
            assumption_release_solver.release_incremental_assumptions();

            const auto result = assumption_release_solver.solve(solve_request {});
            REQUIRE(result.status_of() == solve_result::status::satisfiable);
        }

        SECTION("solver facade stores options and resets session state")
        SECTION("c api adapter maps IPASIR semantics to the facade")
        {
            c_api_adapter adapter;
            adapter.ipasir_init();
            adapter.ipasir_add(1);
            REQUIRE(adapter.ipasir_solve() == 10);
            REQUIRE(adapter.ipasir_val(1) == 1);

            adapter.ipasir_init();
            adapter.ipasir_add(2);
            adapter.ipasir_assume(-2);
            REQUIRE(adapter.ipasir_solve() == 20);
            REQUIRE(adapter.ipasir_failed(-2));
        }

        // removed std::cout: "solver facade test passed\n";
    }

} // namespace
