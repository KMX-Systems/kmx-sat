#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/propagator.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("propagator", "[sat]")
    {
        propagator propagator;
        const clause::ref_t ref {7};

        propagator.attach_clause(ref);
        REQUIRE(propagator.watched_clause_count() == 1u);

        propagator.detach_clause(ref);
        REQUIRE(propagator.watched_clause_count() == 0u);

        propagator.attach_clause(ref);
        REQUIRE(propagator.watched_clause_count() == 1u);
        REQUIRE(propagator.is_attached(ref));
        propagator.attach_clause(ref);
        REQUIRE(propagator.watched_clause_count() == 1u);
        REQUIRE(propagator.is_attached(ref));

        propagator.stage_conflict(ref);
        REQUIRE(propagator.propagate() == ref);
        REQUIRE(propagator.propagate() == clause::ref_t {});

        propagator.set_pending_assumption_count(1u);
        propagator.stage_conflict(ref);
        REQUIRE(propagator.propagate_assumptions() == ref);
        REQUIRE(propagator.beyond_conflict_propagation_call_count() == 0u);

        REQUIRE(propagator.propagate_beyond_conflict() == clause::ref_t {});
        REQUIRE(propagator.propagation_call_count() == 2u);
        REQUIRE(propagator.assumption_propagation_call_count() == 1u);
        REQUIRE(propagator.beyond_conflict_propagation_call_count() == 1u);
    }
}
