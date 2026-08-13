#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/controller/restart.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("restart controller", "[sat]")
    {
        controller::restart restart;
        restart.set_restart_interval(3u);

        restart.tick_conflict();
        REQUIRE(restart.should_restart() == false);
        REQUIRE(restart.current_restart_budget() == 2u);
        REQUIRE(restart.has_pending_restart() == false);

        restart.tick_conflict();
        REQUIRE(restart.should_restart() == false);
        REQUIRE(restart.current_restart_budget() == 1u);

        restart.tick_conflict();
        REQUIRE(restart.should_restart() == true);
        REQUIRE(restart.current_restart_budget() == 0u);

        restart.reset_after_inprocess();
        REQUIRE(restart.restart_count() == 1u);
        REQUIRE(restart.should_restart() == false);

        restart.set_restart_interval(4u);
        restart.tick_conflict();
        restart.tick_conflict();
        restart.reset_after_inprocess();
        REQUIRE(restart.current_restart_budget() == 4u);
        REQUIRE(restart.has_pending_restart() == false);

        restart.set_decision_restart_interval(2u);
        restart.tick_decision();
        REQUIRE(restart.should_restart() == false);
        REQUIRE(restart.current_decision_restart_budget() == 1u);

        restart.tick_decision();
        REQUIRE(restart.should_restart() == true);
        REQUIRE(restart.current_decision_restart_budget() == 0u);

        restart.reset();
        REQUIRE(restart.should_restart() == false);
        REQUIRE(restart.has_pending_restart() == false);
        REQUIRE(restart.current_restart_budget() == 4u);
        REQUIRE(restart.current_decision_restart_budget() == 2u);
        REQUIRE(restart.conflict_count() == 0u);
        REQUIRE(restart.decision_count() == 0u);
        REQUIRE(restart.restart_count() == 0u);

        restart.reset_after_inprocess();
        REQUIRE(restart.should_restart() == false);
        REQUIRE(restart.current_decision_restart_budget() == 2u);
    }

    TEST_CASE("restart controller supports opt-in glue EMA trigger", "[sat]")
    {
        controller::restart restart;
        REQUIRE_FALSE(restart.should_restart());

        restart.set_glue_restart_threshold(1.1);
        for (std::uint32_t index {}; index < 4u; ++index)
        {
            restart.observe_glue(2u);
        }
        REQUIRE_FALSE(restart.should_restart());

        restart.observe_glue(20u);
        REQUIRE(restart.glue_observation_count() == 5u);
        REQUIRE(restart.fast_glue_ema() > restart.slow_glue_ema());
        REQUIRE(restart.should_restart());

        restart.reset();
        REQUIRE_FALSE(restart.should_restart());
        REQUIRE(restart.glue_observation_count() == 0u);
    }
}
