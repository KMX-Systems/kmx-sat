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

        const std::array duplicated_literals {
            literal {variable {3u}, false},
            literal {variable {3u}, false},
            literal {variable {4u}, true},
        };
        const auto dedup_ref = learner.learn_clause(std::span<const literal> {duplicated_literals});
        REQUIRE(dedup_ref.valid() == true);
        REQUIRE(learner.learned_clause_count() == 2u);
        REQUIRE(learner.last_learned_clause().size() == 2u);
        REQUIRE(learner.last_learned_clause()[0].raw() == literal {variable {3u}, false}.raw());
        REQUIRE(learner.last_learned_clause()[1].raw() == literal {variable {4u}, true}.raw());

        const std::array tautological_literals {
            literal {variable {5u}, false},
            literal {variable {5u}, true},
            literal {variable {6u}, false},
        };
        const auto tautology_ref = learner.learn_clause(std::span<const literal> {tautological_literals});
        REQUIRE(tautology_ref.valid() == false);
        REQUIRE(learner.learned_clause_count() == 2u);
    }
}
