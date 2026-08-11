#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/telemetry/ema_tracker.hpp>

namespace kmx::sat::telemetry
{
    TEST_CASE("ema tracker computes moving averages and margins", "[sat]")
    {
        ema_tracker tracker;

        tracker.update_glue_fast(10.0);
        tracker.update_glue_slow(4.0);
        tracker.update_decision_rate(2.0);
        tracker.update_trail(0.5);

        REQUIRE(tracker.fast_vs_slow_margin() > 0.0);
        REQUIRE(tracker.fast_vs_slow_margin() < 10.0);
    }

    TEST_CASE("ema tracker starts at a neutral baseline", "[sat]")
    {
        ema_tracker tracker;

        REQUIRE(tracker.fast_vs_slow_margin() == 0.0);
    }
}
