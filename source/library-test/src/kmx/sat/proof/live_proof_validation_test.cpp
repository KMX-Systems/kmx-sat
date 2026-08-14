#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("live proof validation preserves derived antecedent provenance", "[sat]")
    {
        proof::tracer::drat concrete_sink;
        proof::tracer::view sink {concrete_sink};
        solver solver;
        solver.attach_proof_sink(sink);

        solver.add_clause(
            std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, false}});
        solver.add_clause(std::array<literal, 1> {literal {variable {1u}, true}});
        solver.add_clause(std::array<literal, 1> {literal {variable {2u}, true}});
        solver.add_clause(std::array<literal, 1> {literal {variable {3u}, true}});

        for (std::uint32_t episode {}; episode < 4u; ++episode)
        {
            const auto result = solver.solve(solve_request {});
            REQUIRE(result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(result.proof_summary_of().proof_enabled);
            REQUIRE(result.proof_summary_of().proof_checked);

            const auto events = solver.buffered_proof_events();
            REQUIRE(!events.empty());
            for (std::size_t index {}; index < events.size(); ++index)
            {
                const auto& event = events[index];
                if (event.kind != proof::event_kind::add_derived)
                    continue;
                REQUIRE(!event.antecedent_ids.empty());
                for (const auto antecedent: event.antecedent_ids)
                {
                    bool appeared_earlier = false;
                    for (std::size_t prior {}; prior < index; ++prior)
                    {
                        if ((events[prior].kind == proof::event_kind::add_original ||
                             events[prior].kind == proof::event_kind::add_derived) &&
                            events[prior].clause_id.equals(antecedent))
                        {
                            appeared_earlier = true;
                            break;
                        }
                    }
                    REQUIRE(appeared_earlier);
                }
            }

            solver.reset_session();
            REQUIRE(solver.proof_buffered_event_count() == 0u);
            solver.add_clause(
                std::array<literal, 3> {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, false}});
            solver.add_clause(std::array<literal, 1> {literal {variable {1u}, true}});
            solver.add_clause(std::array<literal, 1> {literal {variable {2u}, true}});
            solver.add_clause(std::array<literal, 1> {literal {variable {3u}, true}});
        }
    }

    TEST_CASE("live proof validation covers SAT and assumption episodes", "[sat]")
    {
        proof::tracer::drat concrete_sink;
        proof::tracer::view sink {concrete_sink};
        solver solver;
        solver.attach_proof_sink(sink);

        const std::array<literal, 1> unit {literal {variable {71u}, false}};
        solver.add_clause(unit);

        const auto sat_result = solver.solve(solve_request {});
        REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);
        REQUIRE(sat_result.proof_summary_of().proof_enabled);
        REQUIRE(sat_result.proof_summary_of().proof_checked);
        REQUIRE(!solver.buffered_proof_events().empty());
        REQUIRE(solver.buffered_proof_events().back().kind == proof::event_kind::conclusion);

        solver.assume(literal {variable {71u}, true});
        const auto assumption_result = solver.solve(solve_request {});
        REQUIRE(assumption_result.status_of() == solve_result::status::unsatisfiable);
        REQUIRE(assumption_result.proof_summary_of().proof_enabled);
        REQUIRE(assumption_result.proof_summary_of().proof_checked);
        REQUIRE(solver.failed(literal {variable {71u}, true}));
        REQUIRE(solver.buffered_proof_events().back().kind == proof::event_kind::conclusion);

        solver.reset_session();
        REQUIRE(solver.proof_buffered_event_count() == 0u);
        solver.add_clause(unit);
        const auto reused_result = solver.solve(solve_request {});
        REQUIRE(reused_result.status_of() == solve_result::status::satisfiable);
        REQUIRE(reused_result.proof_summary_of().proof_enabled);
        REQUIRE(reused_result.proof_summary_of().proof_checked);
        REQUIRE(solver.buffered_proof_events().back().kind == proof::event_kind::conclusion);
    }
}
