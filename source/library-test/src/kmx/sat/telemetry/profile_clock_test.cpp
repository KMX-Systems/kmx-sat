#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/telemetry/profile_clock.hpp>

namespace kmx::sat::telemetry
{
    TEST_CASE("profile clock accumulates nested phase timing", "[sat]")
    {
        profile_clock clock;
        clock.start_phase("search");
        clock.start_phase("propagate");
        clock.stop_phase("propagate");
        clock.stop_phase("search");

        REQUIRE(clock.current_wall_time() >= 1u);
        REQUIRE(clock.current_process_time() >= 1u);
        REQUIRE(clock.phase_count() == 0u);
    }

    TEST_CASE("profile clock ignores unknown phases", "[sat]")
    {
        profile_clock clock;
        clock.start_phase("search");
        clock.stop_phase("unknown");

        REQUIRE(clock.current_wall_time() == 0u);
        REQUIRE(clock.current_process_time() == 0u);
        REQUIRE(clock.phase_count() == 1u);
    }
}
