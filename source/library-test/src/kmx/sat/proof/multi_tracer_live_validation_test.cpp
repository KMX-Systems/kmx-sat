#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/frat.hpp>
#include <kmx/sat/proof/tracer/idrup.hpp>
#include <kmx/sat/proof/tracer/lidrup.hpp>
#include <kmx/sat/proof/tracer/lrat.hpp>
#include <kmx/sat/proof/tracer/veripb.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("live proof validation supports all registered tracer formats", "[sat]")
    {
        using literal_array = std::array<literal, 2>;
        const literal positive {variable {1u}, false};
        const literal negative {variable {1u}, true};
        const literal second {variable {2u}, false};

        struct tracer_case final
        {
            const char* name;
            proof::tracer::view sink;
        };

        proof::tracer::drat drat;
        proof::tracer::lrat lrat;
        proof::tracer::frat frat;
        proof::tracer::idrup idrup;
        proof::tracer::lidrup lidrup;
        proof::tracer::veripb veripb;
        std::array<tracer_case, 6> tracers {
            tracer_case {"drat", proof::tracer::view {drat}},
            tracer_case {"lrat", proof::tracer::view {lrat}},
            tracer_case {"frat", proof::tracer::view {frat}},
            tracer_case {"idrup", proof::tracer::view {idrup}},
            tracer_case {"lidrup", proof::tracer::view {lidrup}},
            tracer_case {"veripb", proof::tracer::view {veripb}},
        };

        for (auto& tracer: tracers)
        {
            solver solver;
            solver.attach_proof_sink(tracer.sink);
            solver.add_clause(literal_array {positive, second});
            solver.add_clause(std::array<literal, 1> {negative});
            solver.add_clause(std::array<literal, 1> {literal {variable {2u}, true}});

            const auto result = solver.solve(solve_request {});
            REQUIRE(result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(result.proof_summary_of().proof_enabled);
            REQUIRE(result.proof_summary_of().proof_checked);
            REQUIRE(solver.proof_buffered_event_count() > 0u);
            REQUIRE(!solver.buffered_proof_events().empty());
            REQUIRE(solver.buffered_proof_events().back().kind == proof::event_kind::conclusion);

            solver.reset_session();
            REQUIRE(solver.proof_buffered_event_count() == 0u);

            solver.add_clause(std::array<literal, 1> {literal {variable {3u}, false}});
            const auto sat_result = solver.solve(solve_request {});
            REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);
            REQUIRE(sat_result.proof_summary_of().proof_enabled);
            REQUIRE(sat_result.proof_summary_of().proof_checked);
            REQUIRE(solver.buffered_proof_events().back().kind == proof::event_kind::conclusion);

            solver.assume(literal {variable {3u}, true});
            const auto assumption_result = solver.solve(solve_request {});
            REQUIRE(assumption_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(assumption_result.proof_summary_of().proof_enabled);
            REQUIRE(assumption_result.proof_summary_of().proof_checked);
            REQUIRE(solver.failed(literal {variable {3u}, true}));
            REQUIRE(solver.buffered_proof_events().back().kind == proof::event_kind::conclusion);

            solver.reset_session();
            REQUIRE(solver.proof_buffered_event_count() == 0u);
        }
    }
}
