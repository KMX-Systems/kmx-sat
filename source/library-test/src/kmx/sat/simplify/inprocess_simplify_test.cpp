#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>
#include <kmx/sat/simplify/eliminator/variable/fast.hpp>
#include <kmx/sat/simplify/factorizer.hpp>
#include <kmx/sat/simplify/scheduler/inprocess.hpp>

namespace kmx::sat::simplify {

TEST_CASE("inprocess simplify", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::simplify;

    scheduler::inprocess scheduler;
    scheduler.set_conflicts_seen(64u);
    scheduler.set_restart_count(2u);
    REQUIRE(scheduler.should_run());
    REQUIRE(scheduler.compute_budget() >= 1u);
    scheduler.run_epoch();
    REQUIRE(scheduler.epoch_count() == 1u);
    REQUIRE(scheduler.last_budget() > 0u);

    eliminator::variable::bounded bounded;
    std::vector<std::vector<literal>> bounded_clauses {
        {literal {variable {1u}, false}, literal {variable {2u}, false}},
        {literal {variable {1u}, true}, literal {variable {3u}, false}}
    };
    bounded.set_clauses(bounded_clauses);
    REQUIRE(bounded.score_variable(variable {1u}) <= 0);
    bounded.run();
    REQUIRE(bounded.elimination_count() == 1u);
    REQUIRE(bounded.eliminated_variables().size() == 1u);

    eliminator::variable::fast fast;
    fast.set_clauses(bounded_clauses);
    REQUIRE(fast.cheap_can_eliminate(variable {1u}));
    fast.run_fast_round();
    REQUIRE(fast.fast_round_count() == 1u);
    REQUIRE(fast.elimination_count() == 1u);

    factorizer factorizer;
    std::vector<std::vector<literal>> factor_clauses {
        {literal {variable {4u}, false}, literal {variable {5u}, false}},
        {literal {variable {4u}, false}, literal {variable {6u}, true}}
    };
    factorizer.set_clauses(factor_clauses);
    factorizer.run();
    REQUIRE(factorizer.introduced_variable_count() == 1u);
    const auto pattern_literal = factorizer.last_pattern_literal();
    REQUIRE((pattern_literal.raw() == literal {variable {4u}, false}.raw()));

    // removed std::cout: "simplify inprocess test passed\n";
    }

} // namespace
