#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/store/constraint.hpp>
#include <kmx/sat/cdcl/witness_checker.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/model_view.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("witness checker validates database clauses and constraint clauses", "[sat]")
    {
        clause::database clauses;
        const std::array literals {
            literal {variable {1u}, false},
            literal {variable {2u}, true},
        };
        clauses.add_clause(std::span<const literal> {literals});

        witness_checker checker;
        checker.attach_clauses(clauses);

        const std::array satisfying_assignments {
            literal {variable {1u}, false},
            literal {variable {2u}, true},
        };
        const model_view satisfying_model {std::span<const literal> {satisfying_assignments}};
        REQUIRE(checker.check_model_against_original(satisfying_model) == true);
        REQUIRE(checker.check_model_against_current(satisfying_model) == true);

        const std::array unsatisfying_assignments {
            literal {variable {1u}, true},
            literal {variable {2u}, false},
        };
        const model_view unsatisfying_model {std::span<const literal> {unsatisfying_assignments}};
        REQUIRE(checker.check_model_against_original(unsatisfying_model) == false);
        REQUIRE(checker.check_model_against_current(unsatisfying_model) == false);

        store::constraint constraint;
        const std::array constraint_literals {
            literal {variable {3u}, false},
        };
        constraint.set_constraint_clause(std::span<const literal> {constraint_literals});
        checker.attach_constraint(constraint);

        const std::array constraint_satisfying_assignments {
            literal {variable {3u}, false},
        };
        const model_view constraint_model {std::span<const literal> {constraint_satisfying_assignments}};
        REQUIRE(checker.check_constraint_satisfaction(constraint_model) == true);
    }
}
