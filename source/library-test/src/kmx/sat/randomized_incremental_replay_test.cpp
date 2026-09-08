#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    struct deterministic_rng final
    {
        std::uint32_t state {0x20260813u};

        std::uint32_t next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return state;
        }
    };

    static bool satisfies(const clause_list_t& clauses, const std::vector<literal>& assumptions, const std::uint32_t assignment) noexcept
    {
        for (const auto& clause: clauses)
        {
            bool satisfied {};
            for (const auto lit: clause)
            {
                const auto bit = (assignment >> (lit.variable_of().index() - 1u)) & 1u;
                satisfied = satisfied || (lit.is_negated() ? bit == 0u : bit != 0u);
            }
            if (!satisfied)
                return false;
        }
        for (const auto assumption: assumptions)
        {
            const auto bit = (assignment >> (assumption.variable_of().index() - 1u)) & 1u;
            if ((assumption.is_negated() && (bit != 0u)) || (!assumption.is_negated() && bit == 0u))
                return false;
        }
        return true;
    }

    static bool oracle_sat(const clause_list_t& clauses, const std::vector<literal>& assumptions,
                           const std::uint32_t variable_count) noexcept
    {
        const auto assignment_count = std::uint32_t {1u} << variable_count;
        for (std::uint32_t assignment {}; assignment < assignment_count; ++assignment)
            if (satisfies(clauses, assumptions, assignment))
                return true;
        return false;
    }

    static std::vector<literal> random_clause(deterministic_rng& rng, const std::uint32_t variable_count)
    {
        constexpr auto size = 1u;
        std::vector<literal> clause;
        while (clause.size() < size)
        {
            const auto variable_index = 1u + rng.next() % variable_count;
            const auto negated = (rng.next() & 1u) != 0u;
            const literal candidate {variable {variable_index}, negated};
            bool duplicate {};
            for (const auto existing: clause)
                duplicate = duplicate || (existing.raw() == candidate.raw());
            if (!duplicate)
                clause.push_back(candidate);
        }
        return clause;
    }

    TEST_CASE("randomized incremental replay matches brute-force oracle", "[sat]")
    {
        constexpr std::uint32_t variable_count {4u};
        deterministic_rng rng;
        solver solver;
        solver.set_option(option_id::chb_enabled, 1L);
        solver.set_option(option_id::reduction_fraction_percent, 30L);
        clause_list_t clauses;
        bool configured_chb_enabled = true;
        auto previous_statistics = solver.statistics();

        for (std::uint32_t episode {}; episode < 96u; ++episode)
        {
            if ((episode != 0u) && (episode % 8u == 0u))
            {
                solver.reset_session();
                clauses.clear();
                REQUIRE(solver.chb_enabled() == configured_chb_enabled);
                REQUIRE(solver.reduction_fraction_percent() == 30u);
                previous_statistics = solver.statistics();
            }

            if ((episode % 3u) == 0u)
            {
                configured_chb_enabled = (episode % 2u) == 0u;
                solver.set_option(option_id::chb_enabled, configured_chb_enabled ? 1 : 0);
                REQUIRE(solver.has_persisted_configuration());
            }

            const auto additions = 1u + rng.next() % 2u;
            for (std::uint32_t index {}; index < additions; ++index)
            {
                auto clause = random_clause(rng, variable_count);
                solver.add_clause(std::span<const literal> {clause});
                clauses.push_back(std::move(clause));
            }

            std::vector<literal> assumptions;
            if ((rng.next() % 3u) == 0u)
            {
                assumptions.push_back(literal {variable {1u + rng.next() % variable_count}, (rng.next() & 1u) != 0u});
                solver.assume(assumptions.back());
            }
            if ((rng.next() % 5u) == 0u)
            {
                assumptions.push_back(literal {variable {1u + rng.next() % variable_count}, (rng.next() & 1u) != 0u});
                solver.assume(assumptions.back());
            }

            solve_request request;
            request.decision_limit = 4u;
            const auto result = solver.solve(request);
            const auto expected_sat = oracle_sat(clauses, assumptions, variable_count);
            if (result.status_of() == solve_result::status::unknown)
                REQUIRE(request.decision_limit != 0u);
            else
                REQUIRE(result.status_of() == (expected_sat ? solve_result::status::satisfiable : solve_result::status::unsatisfiable));
            const auto current_statistics = solver.statistics();
            REQUIRE(telemetry::solver_statistics::snapshot_monotonic(previous_statistics, current_statistics));
            previous_statistics = current_statistics;
            if (result.status_of() == solve_result::status::satisfiable)
                REQUIRE(!result.model().values().empty());
            if ((result.status_of() == solve_result::status::unsatisfiable) && !assumptions.empty())
                REQUIRE(!result.failed_core().assumptions().empty());
            solver.release_incremental_assumptions();
        }
    }
}
