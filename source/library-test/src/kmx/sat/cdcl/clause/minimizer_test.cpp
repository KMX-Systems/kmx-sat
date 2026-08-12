#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/clause/minimizer.hpp>
#include <kmx/sat/cdcl/clause/storage.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("clause minimizer", "[sat]")
    {
        using namespace kmx::sat;

        clause::storage storage;
        clause::database database;
        clause::minimizer minimizer;
        minimizer.attach_storage(storage);
        minimizer.attach_database(database);

        const variable var_a {1};
        const variable var_b {2};
        const literal a_pos {var_a, false};
        const literal b_pos {var_b, false};

        const std::array<literal, 3> literals {a_pos, b_pos, a_pos};
        const auto ref = storage.create_learned_clause(literals);
        REQUIRE(ref.valid());
        REQUIRE(database.tier_of(ref) == clause::database::default_tier);

        minimizer.minimize_learned_clause(ref);
        minimizer.shrink_clause(ref);
        minimizer.recompute_glue(ref);
        minimizer.promote_if_needed(ref);

        const auto stored_literals = storage.literals_of(ref);
        REQUIRE(stored_literals.size() == 2u);
        REQUIRE(minimizer.minimized_clause_count() == 1u);
        REQUIRE(minimizer.shrunk_clause_count() == 1u);
        REQUIRE(minimizer.last_glue() == 2u);
        REQUIRE(minimizer.promoted_clause_count() == 1u);
        REQUIRE(database.tier_of(ref) == 0u);
    }
}
