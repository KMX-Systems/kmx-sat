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
    }
}
