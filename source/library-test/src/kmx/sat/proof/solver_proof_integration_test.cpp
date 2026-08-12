#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("solver proof integration", "[sat]")
    {
        proof::tracer::drat concrete_sink;
        proof::tracer::view sink {concrete_sink};
        solver proof_solver;
        proof_solver.attach_proof_sink(sink);

        std::vector<literal> proof_clause {literal {variable {4u}, false}};
        proof_solver.add_clause(std::span<const literal> {proof_clause});
        const auto proof_result = proof_solver.solve(solve_request {});

        const auto proof_summary = proof_result.proof_summary_of();
        REQUIRE(proof_summary.proof_enabled);
        REQUIRE(proof_summary.proof_checked);

        proof_solver.reset_session();
        proof_solver.add_clause(std::span<const literal> {proof_clause});
        const auto proof_result_after_reset = proof_solver.solve(solve_request {});
        REQUIRE(proof_result_after_reset.proof_summary_of().proof_enabled);
        REQUIRE(proof_result_after_reset.proof_summary_of().proof_checked);
    }
}