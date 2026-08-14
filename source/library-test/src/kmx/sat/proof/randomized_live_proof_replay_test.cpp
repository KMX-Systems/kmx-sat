#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/event_stream.hpp>
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("randomized live proof replay resets complete event state", "[sat]")
    {
        proof::tracer::drat concrete_sink;
        proof::tracer::view sink {concrete_sink};
        solver solver;
        solver.attach_proof_sink(sink);

        for (std::uint32_t episode {}; episode < 8u; ++episode)
        {
            const auto first = variable {100u + episode * 3u + 1u};
            const auto second = variable {100u + episode * 3u + 2u};
            const auto third = variable {100u + episode * 3u + 3u};
            solver.add_clause(std::array<literal, 3> {literal {first, false}, literal {second, false}, literal {third, false}});
            solver.add_clause(std::array<literal, 1> {literal {first, true}});
            solver.add_clause(std::array<literal, 1> {literal {second, true}});
            solver.add_clause(std::array<literal, 1> {literal {third, true}});

            const auto result = solver.solve(solve_request {});
            REQUIRE(result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(result.proof_summary_of().proof_enabled);
            REQUIRE(result.proof_summary_of().proof_checked);

            const auto events = solver.buffered_proof_events();
            REQUIRE(!events.empty());
            REQUIRE(events.back().kind == proof::event_kind::conclusion);
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
                        const auto& prior_event = events[prior];
                        if ((prior_event.kind == proof::event_kind::add_original || prior_event.kind == proof::event_kind::add_derived) &&
                            prior_event.clause_id.equals(antecedent))
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
        }
    }
}
