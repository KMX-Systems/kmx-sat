#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/engine/decision.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("decision engine", "[sat]")
    {
        engine::decision decision;
        decision.set_next_variable(7u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 7u);
        REQUIRE(branch->is_negated() == false);

        const auto variable = decision.pick_decision_variable();
        REQUIRE(variable.has_value());
        REQUIRE(variable->variable_of().index() == 7u);

        REQUIRE(decision.pick_decision_phase() == true);
        decision.notify_conflict();
        decision.notify_restart();
        decision.notify_rephase();
        decision.select_heuristic_blend();
        REQUIRE(decision.has_active_blend());
        const auto selected = decision.last_selected_variable();
        REQUIRE(selected.has_value());
        REQUIRE(selected->index() == 7u);
    }
}
