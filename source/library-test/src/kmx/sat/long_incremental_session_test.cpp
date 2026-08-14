#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("long incremental session preserves SAT/UNSAT and reset semantics", "[sat]")
    {
        const std::array<literal, 1> unit_clause {literal {variable {1u}, false}};
        solver solver;
        solver.add_clause(unit_clause);
        solver.set_option("chb_enabled", 1);
        solver.set_option("reduction_fraction_percent", 25);

        for (std::uint32_t episode {}; episode < 256u; ++episode)
        {
            solver.assume(literal {variable {1u}, true});
            const auto unsat_result = solver.solve(solve_request {});
            REQUIRE(unsat_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(solver.failed(literal {variable {1u}, true}));
            const auto after_unsat_statistics = solver.statistics();

            solver.release_incremental_assumptions();
            const auto sat_result = solver.solve(solve_request {});
            REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);
            REQUIRE(solver.value_of(variable {1u}).has_value());
            REQUIRE(*solver.value_of(variable {1u}));
            REQUIRE(telemetry::solver_statistics::snapshot_monotonic(after_unsat_statistics, solver.statistics()));
            REQUIRE(solver.statistics_report_emission_count() <= 64u);
            REQUIRE(solver.chb_enabled());
            REQUIRE(solver.reduction_fraction_percent() == 25u);

            if ((episode + 1u) % 8u == 0u)
            {
                solver.reset_session();
                REQUIRE(solver.current_state() == solver_state_machine::state::configuring);
                REQUIRE(solver.chb_enabled());
                REQUIRE(solver.reduction_fraction_percent() == 25u);
                solver.add_clause(unit_clause);
            }
        }
    }

    TEST_CASE("long incremental session isolates clauses, limits, and assumptions", "[sat]")
    {
        const std::array<literal, 2> branching_clause {literal {variable {1u}, false}, literal {variable {2u}, false}};
        const std::array<literal, 1> first_unit {literal {variable {1u}, false}};
        const std::array<literal, 1> second_unit {literal {variable {3u}, false}};

        solver solver;
        solver.set_option("reduction_fraction_percent", 30);

        for (std::uint32_t episode {}; episode < 256u; ++episode)
        {
            solver.reset_session();
            solver.add_clause(branching_clause);
            solve_request limited_request;
            limited_request.decision_limit = 1u;
            const auto limited = solver.solve(limited_request);
            REQUIRE(limited.status_of() == solve_result::status::unknown);
            REQUIRE(solver.statistics_report_emission_count() <= 64u);

            solver.reset_session();
            solver.add_clause(first_unit);
            solver.assume(literal {variable {1u}, true});
            const auto assumption_result = solver.solve(solve_request {});
            REQUIRE(assumption_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(solver.failed(literal {variable {1u}, true}));
            solver.release_incremental_assumptions();

            solver.reset_session();
            REQUIRE(solver.current_state() == solver_state_machine::state::configuring);
            solver.add_clause(second_unit);
            const auto replacement_result = solver.solve(solve_request {});
            REQUIRE(replacement_result.status_of() == solve_result::status::satisfiable);
            REQUIRE(solver.value_of(variable {3u}).value_or(false));
            REQUIRE(solver.reduction_fraction_percent() == 30u);
            REQUIRE(solver.statistics_report_emission_count() <= 64u);
        }
    }
}
