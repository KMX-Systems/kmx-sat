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
        REQUIRE(reduce.last_selected_candidate_count() == 0u);
        REQUIRE_FALSE(reduce.has_candidates());

        reduce.reduce_clauses();
        reduce.flush_redundant();
        reduce.update_tiers();

        REQUIRE(reduce.reduction_pass_count() == 1u);
        REQUIRE(reduce.reduced_candidates() == 0u);
        REQUIRE(reduce.flushed_candidates() == 0u);
        REQUIRE(reduce.should_reduce() == false);

        reduce.request_reduce();
        reduce.select_reduction_candidates();
        reduce.reduce_clauses();
        reduce.flush_redundant();
        reduce.update_tiers();

        REQUIRE(reduce.reduction_pass_count() == 2u);
        REQUIRE(reduce.reduced_candidates() == 0u);
        REQUIRE(reduce.flushed_candidates() == 0u);

        reduce.request_reduce();
        REQUIRE(reduce.should_reduce());
        reduce.reset();
        REQUIRE_FALSE(reduce.should_reduce());
        REQUIRE(reduce.reduction_pass_count() == 0u);
        REQUIRE(reduce.last_selected_candidate_count() == 0u);
        REQUIRE(reduce.reduced_candidates() == 0u);
        REQUIRE(reduce.flushed_candidates() == 0u);
        REQUIRE_FALSE(reduce.has_candidates());
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

        database.clear_reason_clauses();
        REQUIRE(database.is_reason_clause(reason_ref) == false);

        reduce.flush_redundant(database);
        reduce.update_tiers();

        REQUIRE(reduce.reduced_candidates() == 1u);
        REQUIRE(reduce.flushed_candidates() == 1u);

        std::size_t remaining_redundant_count {};
        bool reason_survived {};
        database.iterate_redundant(
            [&](const clause::ref_t ref) noexcept
            {
                ++remaining_redundant_count;
                if (ref == reason_ref)
                    reason_survived = true;
            });

        REQUIRE(remaining_redundant_count == 1u);
        REQUIRE(reason_survived);
    }

    TEST_CASE("reduce controller ranks quality metadata", "[sat]")
    {
        controller::reduce reduce;
        clause::database database;

        const auto low_glue_used =
            database.add_clause(std::array<literal, 2> {literal {variable {10}, false}, literal {variable {11}, false}}, true);
        const auto high_glue_unused =
            database.add_clause(std::array<literal, 4> {literal {variable {12}, false}, literal {variable {13}, false},
                                                        literal {variable {14}, false}, literal {variable {15}, false}},
                                true);
        const auto active_reason = database.add_clause(std::array<literal, 1> {literal {variable {16}, false}}, true);

        database.set_glue(low_glue_used, 1u);
        database.set_glue(high_glue_unused, 4u);
        database.increment_used_count(low_glue_used);
        database.increment_used_count(low_glue_used);
        database.mark_reason_clause(active_reason);

        const std::array<clause::ref_t, 3> candidates {low_glue_used, high_glue_unused, active_reason};
        reduce.set_reduction_fraction_percent(100u);
        reduce.select_reduction_candidates(database, candidates);

        const auto ranked = reduce.candidate_offsets();
        REQUIRE(ranked.size() == 2u);
        REQUIRE(ranked[0] == high_glue_unused.offset());
        REQUIRE(ranked[1] == low_glue_used.offset());
        REQUIRE(reduce.last_selected_candidate_count() == 2u);
    }

    TEST_CASE("reduce controller ranks inactive clauses before active ties", "[sat]")
    {
        controller::reduce reduce;
        clause::database database;
        const auto inactive =
            database.add_clause(std::array<literal, 2> {literal {variable {40u}, false}, literal {variable {41u}, false}}, true);
        const auto active =
            database.add_clause(std::array<literal, 2> {literal {variable {42u}, false}, literal {variable {43u}, false}}, true);

        database.set_glue(inactive, 3u);
        database.set_glue(active, 3u);
        database.increment_activity(active, 4.0);

        const std::array<clause::ref_t, 2> candidates {active, inactive};
        reduce.set_reduction_fraction_percent(100u);
        reduce.select_reduction_candidates(database, candidates);

        const auto ranked = reduce.candidate_offsets();
        REQUIRE(ranked.size() == 2u);
        REQUIRE(ranked[0] == inactive.offset());
        REQUIRE(ranked[1] == active.offset());
    }

    TEST_CASE("reduce controller protects active low-glue clauses", "[sat]")
    {
        controller::reduce reduce;
        clause::database database;
        const auto active_low_glue =
            database.add_clause(std::array<literal, 2> {literal {variable {60u}, false}, literal {variable {61u}, false}}, true);
        const auto stale_low_glue =
            database.add_clause(std::array<literal, 2> {literal {variable {62u}, false}, literal {variable {63u}, false}}, true);

        database.set_glue(active_low_glue, 2u);
        database.set_glue(stale_low_glue, 2u);
        database.increment_activity(active_low_glue, 3.0);
        reduce.set_reduction_fraction_percent(100u);

        const std::array<clause::ref_t, 2> candidates {active_low_glue, stale_low_glue};
        reduce.select_reduction_candidates(database, candidates);

        REQUIRE(reduce.activity_retention_threshold() == 2.0);
        REQUIRE(reduce.candidate_offsets().size() == 1u);
        REQUIRE(reduce.candidate_offsets().front() == stale_low_glue.offset());
    }

    TEST_CASE("reduce controller applies adaptive deletion quota", "[sat]")
    {
        controller::reduce reduce;
        clause::database database;
        std::array<clause::ref_t, 4> candidates {};
        for (std::size_t index {}; index < candidates.size(); ++index)
        {
            candidates[index] =
                database.add_clause(std::array<literal, 2> {literal {variable {static_cast<std::uint32_t>(50u + index * 2u)}, false},
                                                            literal {variable {static_cast<std::uint32_t>(51u + index * 2u)}, false}},
                                    true);
            database.set_glue(candidates[index], static_cast<std::uint32_t>(index + 1u));
        }

        // The shipped default is 75 percent; this scenario exercises the quota arithmetic at half.
        reduce.set_reduction_fraction_percent(50u);
        reduce.select_reduction_candidates(database, candidates);
        REQUIRE(reduce.reduction_fraction_percent() == 50u);
        REQUIRE(reduce.candidate_offsets().size() == 2u);

        reduce.set_reduction_fraction_percent(25u);
        reduce.select_reduction_candidates(database, candidates);
        REQUIRE(reduce.candidate_offsets().size() == 1u);

        database.increment_activity(candidates[0], 8.0);
        database.increment_used_count(candidates[0]);
        database.decay_quality();
        const auto aged = database.quality_of(candidates[0]);
        REQUIRE(aged.activity == 4.0);
        REQUIRE(aged.used_count == 0u);
    }
}
