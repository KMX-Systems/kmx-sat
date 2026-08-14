#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/solver_core.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    struct deterministic_rng final
    {
        std::uint32_t state {0x31415926u};

        std::uint32_t next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return state;
        }
    };

    static bool brute_force_sat(const std::vector<std::vector<literal>>& clauses, const std::uint32_t variable_count) noexcept
    {
        const auto assignment_count = std::uint32_t {1u} << variable_count;
        for (std::uint32_t assignment {}; assignment < assignment_count; ++assignment)
        {
            bool formula_satisfied = true;
            for (const auto& clause: clauses)
            {
                bool clause_satisfied = false;
                for (const auto lit: clause)
                {
                    const auto variable_bit = std::uint32_t {1u} << (lit.variable_of().index() - 1u);
                    const auto value = (assignment & variable_bit) != 0u;
                    const auto literal_value = lit.is_negated() ? !value : value;
                    clause_satisfied = clause_satisfied || literal_value;
                }
                if (!clause_satisfied)
                {
                    formula_satisfied = false;
                    break;
                }
            }
            if (formula_satisfied)
                return true;
        }
        return false;
    }

    TEST_CASE("propagation hardening matches brute-force replay on deterministic small CNFs", "[sat]")
    {
        deterministic_rng rng;
        constexpr std::uint32_t variable_count {4u};

        for (std::uint32_t formula_index {}; formula_index < 32u; ++formula_index)
        {
            solver_core solver;
            std::vector<std::vector<literal>> clauses {};
            const auto clause_count = 5u + (rng.next() % 8u);

            for (std::uint32_t clause_index {}; clause_index < clause_count; ++clause_index)
            {
                const auto clause_size = 1u + (rng.next() % 3u);
                std::vector<literal> clause {};
                for (std::uint32_t literal_index {}; literal_index < clause_size; ++literal_index)
                {
                    const auto variable_index = 1u + (rng.next() % variable_count);
                    const auto negated = (rng.next() & 1u) != 0u;
                    clause.push_back(literal {variable {variable_index}, negated});
                }
                solver.add_problem_clause(std::span<const literal> {clause});
                clauses.push_back(std::move(clause));
            }

            const auto expected_sat = brute_force_sat(clauses, variable_count);
            const auto actual_status = solver.solve({});
            const auto actual_sat = actual_status == solver_core::status::satisfiable;
            REQUIRE(actual_sat == expected_sat);
        }
    }

    TEST_CASE("propagation hardening survives repeated solve episodes", "[sat]")
    {
        solver_core solver;
        solver.add_problem_clause({literal {variable {1u}, false}, literal {variable {2u}, false}});
        solver.add_problem_clause({literal {variable {1u}, true}, literal {variable {3u}, false}});
        solver.add_problem_clause({literal {variable {2u}, true}, literal {variable {4u}, false}});
        solver.add_problem_clause({literal {variable {3u}, true}, literal {variable {4u}, false}});

        for (std::uint32_t episode {}; episode < 8u; ++episode)
        {
            const auto status = solver.solve({});
            REQUIRE(status == solver_core::status::satisfiable);
            REQUIRE(solver.current_search_outcome() == search_coordinator::outcome::satisfiable);
            REQUIRE(solver.current_status() == solver_core::status::satisfiable);
            REQUIRE(solver.watch_entry_scan_count() > 0u);
        }
    }

    TEST_CASE("live CDCL handles deep branching contradiction without units", "[sat]")
    {
        constexpr std::uint32_t variable_count {4u};
        solver_core solver;

        // Forbid every assignment with one 4-literal clause. No unit clause assists the root propagation phase.
        for (std::uint32_t assignment {}; assignment < (std::uint32_t {1u} << variable_count); ++assignment)
        {
            std::array<literal, variable_count> forbidden {};
            for (std::uint32_t index {}; index < variable_count; ++index)
            {
                const auto value = ((assignment >> index) & 1u) != 0u;
                forbidden[index] = literal {variable {index + 1u}, value};
            }
            solver.add_problem_clause(std::span<const literal> {forbidden});
        }

        const auto result = solver.solve({});
        REQUIRE(result == solver_core::status::unsatisfiable);
        REQUIRE(solver.current_search_outcome() == search_coordinator::outcome::unsatisfiable);
        REQUIRE(solver.propagator_call_count() > 0u);
        REQUIRE(solver.learned_clause_count() > 0u);
    }
}
