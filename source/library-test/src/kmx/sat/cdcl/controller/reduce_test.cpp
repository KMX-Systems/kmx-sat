#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/controller/reduce.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("reduce controller", "[sat]")
    {
        controller::reduce reduce;
        REQUIRE(reduce.should_reduce() == false);

        reduce.request_reduce();
        REQUIRE(reduce.should_reduce() == true);

        reduce.select_reduction_candidates();
        REQUIRE(reduce.last_selected_candidate_count() == 1u);

        reduce.reduce_clauses();
        reduce.flush_redundant();
        reduce.update_tiers();

        REQUIRE(reduce.reduction_pass_count() == 1u);
        REQUIRE(reduce.reduced_candidates() == 1u);
        REQUIRE(reduce.flushed_candidates() == 1u);
        REQUIRE(reduce.should_reduce() == false);
    }

    TEST_CASE("reduce controller uses clause database state", "[sat]")
    {
        controller::reduce reduce;
        clause::database database;

        const variable var_a {1};
        const variable var_b {2};
        const literal a_pos {var_a, false};
        const literal b_pos {var_b, false};

        const std::array<literal, 2> keep_literals {a_pos, b_pos};
        const auto keep_ref = database.add_clause(keep_literals, true);
        const auto reason_ref = database.add_clause(std::array<literal, 1> {a_pos}, true);

        database.demote_clause(keep_ref);
        database.demote_clause(keep_ref);
        database.mark_reason_clause(reason_ref);

        const std::array<clause::ref_t, 2> candidates {keep_ref, reason_ref};
        reduce.select_reduction_candidates(database, candidates);
        REQUIRE(reduce.last_selected_candidate_count() == 1u);
        REQUIRE(reduce.has_candidates());
        REQUIRE(database.is_garbage(keep_ref) == false);

        reduce.reduce_clauses(database);
        REQUIRE(database.is_garbage(keep_ref));
        REQUIRE(database.is_garbage(reason_ref) == false);

        reduce.flush_redundant(database);
        reduce.update_tiers();

        REQUIRE(reduce.reduced_candidates() == 1u);
        REQUIRE(reduce.flushed_candidates() == 1u);

        std::size_t remaining_redundant_count {0u};
        bool reason_survived {false};
        database.iterate_redundant(
            [&](const clause::ref_t ref) noexcept
            {
                ++remaining_redundant_count;
                if (ref == reason_ref)
                {
                    reason_survived = true;
                }
            });

        REQUIRE(remaining_redundant_count == 1u);
        REQUIRE(reason_survived);
    }
}
