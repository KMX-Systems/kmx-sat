/// @file library-test/src/kmx/sat/cdcl/controller/reduce_protection_test.cpp
/// @brief Recent-use protection of learned clauses in the reduction pass.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/controller/reduce.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    namespace
    {
        clause::ref_t add_learned(clause::database& database, const std::uint32_t first_variable, const std::uint32_t glue)
        {
            const std::array<literal, 4u> literals {
                literal {variable {first_variable}, false}, literal {variable {first_variable + 1u}, false},
                literal {variable {first_variable + 2u}, true}, literal {variable {first_variable + 3u}, false}};
            const auto ref = database.add_clause(literals, true);
            database.set_glue(ref, glue);
            return ref;
        }

        bool is_candidate(controller::reduce& reduce, const clause::database& database, const clause::ref_t ref)
        {
            reduce.select_reduction_candidates(database);
            const auto offsets = reduce.candidate_offsets();
            return std::find(offsets.begin(), offsets.end(), ref.offset()) != offsets.end();
        }
    }

    TEST_CASE("a clause used since the last pass is not a reduction candidate", "[sat][regression]")
    {
        // At a fixed 1,000-conflict interval without this protection `hole9` took a million conflicts, relearning
        // what each pass had thrown away; growing the interval instead cost the random set 9-20%.
        controller::reduce reduce;
        reduce.set_reduction_fraction_percent(100u);
        clause::database database;
        const auto used = add_learned(database, 1u, 8u);
        const auto idle_a = add_learned(database, 10u, 8u);
        const auto idle_b = add_learned(database, 20u, 8u);
        database.note_clause_used(used);

        REQUIRE_FALSE(is_candidate(reduce, database, used));
        REQUIRE(is_candidate(reduce, database, idle_a));
        REQUIRE(is_candidate(reduce, database, idle_b));

        reduce.reduce_clauses(database);
        REQUIRE_FALSE(database.is_garbage(used));
        REQUIRE(database.is_garbage(idle_a));
        REQUIRE(database.is_garbage(idle_b));
    }

    TEST_CASE("usage is counted per pass, with one pass of grace for mid-glue clauses", "[sat][regression]")
    {
        // A use marks the clause for the next pass; a mid-glue clause keeps one further pass, a high-glue one
        // must earn its place again. The first version of the rule kept every used mid-glue clause forever,
        // because the grace mark looked exactly like a fresh use.
        controller::reduce reduce;
        reduce.set_reduction_fraction_percent(100u);
        clause::database database;
        const auto mid_glue = add_learned(database, 1u, 4u);
        const auto high_glue = add_learned(database, 10u, 9u);
        database.note_clause_used(mid_glue);
        database.note_clause_used(high_glue);

        REQUIRE_FALSE(is_candidate(reduce, database, mid_glue));
        REQUIRE_FALSE(is_candidate(reduce, database, high_glue));

        reduce.update_tiers(database); // first pass after the use
        REQUIRE_FALSE(is_candidate(reduce, database, mid_glue));
        REQUIRE(is_candidate(reduce, database, high_glue));

        reduce.update_tiers(database); // the grace pass is over
        REQUIRE(is_candidate(reduce, database, mid_glue));
        REQUIRE(is_candidate(reduce, database, high_glue));
    }
}
