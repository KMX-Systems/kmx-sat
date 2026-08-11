#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/clause/learner.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("clause learner records and exposes learned clauses", "[sat]")
    {
        clause::learner learner;

        const std::array literals {
            literal {variable {1u}, false},
            literal {variable {2u}, true},
        };
        const auto ref = learner.learn_clause(std::span<const literal> {literals});

        REQUIRE(ref.valid() == true);
        REQUIRE(learner.learned_clause_count() == 1u);
        REQUIRE(learner.last_learned_clause().size() == 2u);
        REQUIRE(learner.last_learned_clause()[0].raw() == literals[0].raw());
        REQUIRE(learner.last_learned_clause()[1].raw() == literals[1].raw());
        REQUIRE(learner.clause_count_for_size(2u) == 1u);

        learner.assign_asserting_literal(literal {variable {2u}, false});
        REQUIRE(learner.last_asserting_literal().raw() == literal {variable {2u}, false}.raw());
    }
}
