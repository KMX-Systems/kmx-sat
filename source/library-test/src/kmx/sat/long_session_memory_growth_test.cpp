#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    namespace
    {
        std::size_t resident_set_kb() noexcept
        {
            std::ifstream status {"/proc/self/status"};
            std::string name;
            std::size_t value {};
            std::string unit;
            while (status >> name >> value >> unit)
            {
                if (name == "VmRSS:")
                {
                    return value;
                }
            }
            return 0u;
        }
    }

    TEST_CASE("long incremental session keeps memory and telemetry bounded", "[sat]")
    {
        const std::array<literal, 1> unit_clause {literal {variable {1u}, false}};
        solver solver;
        solver.set_option("chb_enabled", 1);
        solver.set_option("reduction_fraction_percent", 30);
        solver.add_clause(unit_clause);

        const auto initial_rss_kb = resident_set_kb();
        auto peak_rss_kb = initial_rss_kb;
        for (std::uint32_t episode {}; episode < 1000u; ++episode)
        {
            solver.assume(literal {variable {1u}, true});
            const auto unsat_result = solver.solve(solve_request {});
            REQUIRE(unsat_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(solver.failed(literal {variable {1u}, true}));
            solver.release_incremental_assumptions();

            const auto sat_result = solver.solve(solve_request {});
            REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);
            REQUIRE(solver.value_of(variable {1u}).value_or(false));
            REQUIRE(solver.statistics_report_emission_count() <= 64u);
            peak_rss_kb = std::max(peak_rss_kb, resident_set_kb());

            if ((episode + 1u) % 32u == 0u)
            {
                solver.reset_session();
                REQUIRE(solver.current_state() == solver_state_machine::state::configuring);
                solver.set_option("chb_enabled", 1);
                solver.set_option("reduction_fraction_percent", 30);
                solver.add_clause(unit_clause);
            }
        }

        REQUIRE(peak_rss_kb >= initial_rss_kb);
        REQUIRE(peak_rss_kb <= initial_rss_kb + 64u * 1024u);
    }
}
