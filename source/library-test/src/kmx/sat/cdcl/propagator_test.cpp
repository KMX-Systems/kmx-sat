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
        REQUIRE(propagator.has_staged_conflicts());
        REQUIRE(propagator.staged_conflict_count() == 1u);
        REQUIRE(propagator.propagate() == ref);
        REQUIRE_FALSE(propagator.has_staged_conflicts());
        REQUIRE(propagator.staged_conflict_count() == 0u);
        REQUIRE(propagator.propagate() == clause::ref_t {});

        propagator.set_pending_assumption_count(1u);
        propagator.stage_conflict(ref);
        REQUIRE(propagator.staged_conflict_count() == 1u);
        REQUIRE(propagator.propagate_assumptions() == ref);
        REQUIRE(propagator.staged_conflict_count() == 0u);
        REQUIRE(propagator.beyond_conflict_propagation_call_count() == 0u);

        propagator.stage_conflict(ref);
        REQUIRE(propagator.staged_conflict_count() == 1u);
        propagator.set_pending_assumption_count(1u);
        REQUIRE(propagator.propagate_assumptions() == ref);
        REQUIRE(propagator.staged_conflict_count() == 0u);

        const clause::ref_t ref_b {8};
        const clause::ref_t ref_c {9};
        propagator.stage_conflict(ref);
        propagator.stage_conflict(ref_b);
        propagator.stage_conflict(ref_c);
        REQUIRE(propagator.staged_conflict_count() == 3u);
        REQUIRE(propagator.propagate_beyond_conflict() == ref);
        REQUIRE(propagator.staged_conflict_count() == 2u);
        REQUIRE(propagator.propagate() == ref_b);
        REQUIRE(propagator.staged_conflict_count() == 1u);
        propagator.set_pending_assumption_count(1u);
        REQUIRE(propagator.propagate_assumptions() == ref_c);
        REQUIRE_FALSE(propagator.has_staged_conflicts());
        REQUIRE(propagator.staged_conflict_count() == 0u);

        REQUIRE(propagator.propagate_beyond_conflict() == clause::ref_t {});
        REQUIRE(propagator.propagation_call_count() == 3u);
        REQUIRE(propagator.assumption_propagation_call_count() == 3u);
        REQUIRE(propagator.beyond_conflict_propagation_call_count() == 2u);
    }
}
