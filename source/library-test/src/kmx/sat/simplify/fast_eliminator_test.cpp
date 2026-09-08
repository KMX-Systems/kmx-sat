#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/simplify/eliminator/variable/fast.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify::eliminator::variable
{
    TEST_CASE("fast eliminator tracks cheap scores and rounds", "[sat]")
    {
        fast eliminator;
        clause_list_t clauses;
        std::vector<literal> clause_one;
        clause_one.push_back(::kmx::sat::literal {::kmx::sat::variable {1u}, false});
        clause_one.push_back(::kmx::sat::literal {::kmx::sat::variable {2u}, false});
        clauses.push_back(clause_one);

        std::vector<literal> clause_two;
        clause_two.push_back(::kmx::sat::literal {::kmx::sat::variable {1u}, true});
        clause_two.push_back(::kmx::sat::literal {::kmx::sat::variable {3u}, false});
        clauses.push_back(clause_two);

        eliminator.set_clauses(clauses);

        REQUIRE(eliminator.cheap_score_variable(::kmx::sat::variable {1u}) <= 0L);
        REQUIRE(eliminator.cheap_can_eliminate(::kmx::sat::variable {1u}));

        clause_list_t expensive_clauses;
        std::vector<literal> expensive_clause_one;
        expensive_clause_one.push_back(::kmx::sat::literal {::kmx::sat::variable {4u}, false});
        expensive_clause_one.push_back(::kmx::sat::literal {::kmx::sat::variable {5u}, false});
        expensive_clauses.push_back(expensive_clause_one);

        std::vector<literal> expensive_clause_two;
        expensive_clause_two.push_back(::kmx::sat::literal {::kmx::sat::variable {4u}, true});
        expensive_clause_two.push_back(::kmx::sat::literal {::kmx::sat::variable {6u}, false});
        expensive_clauses.push_back(expensive_clause_two);
        eliminator.set_clauses(expensive_clauses);

        REQUIRE(eliminator.cheap_score_variable(::kmx::sat::variable {4u}) >= 0L);
        REQUIRE(eliminator.cheap_can_eliminate(::kmx::sat::variable {4u}));

        eliminator.run_fast_round();
        REQUIRE(eliminator.fast_round_count() == 1u);
        REQUIRE(eliminator.elimination_count() == 1u);
    }
}
