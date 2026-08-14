#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/chb_tracker.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("chb tracker accumulates and decays scores", "[sat]")
    {
        chb_tracker tracker;
        const variable first {4u};
        const variable second {9u};

        tracker.update_on_assignment(first);
        tracker.update_on_conflict(first);
        tracker.update_on_conflict(second);

        REQUIRE(tracker.score_of(first) > tracker.score_of(second));

        tracker.decay_step();

        REQUIRE(tracker.score_of(first) > 0.0);
        REQUIRE(tracker.score_of(second) > 0.0);
        REQUIRE(tracker.tracked_variable_count() == 2u);
        REQUIRE(tracker.decay_count() == 1u);
    }

    TEST_CASE("chb tracker returns zero for untracked variables", "[sat]")
    {
        chb_tracker tracker;
        const variable untracked {77u};

        REQUIRE(tracker.score_of(untracked) == 0.0);
        REQUIRE(tracker.tracked_variable_count() == 0u);
    }

    TEST_CASE("chb tracker uses bounded reward updates", "[sat]")
    {
        chb_tracker tracker;
        tracker.set_learning_rate(0.25);
        tracker.set_decay_factor(0.5);
        const variable var {81u};

        for (int index {}; index < 20; ++index)
            tracker.update_on_conflict(var);

        REQUIRE(tracker.score_of(var) > 0.0);
        REQUIRE(tracker.score_of(var) <= 1.0);
        tracker.decay_step();
        REQUIRE(tracker.score_of(var) < 1.0);
        REQUIRE(tracker.decay_count() == 1u);
    }
}
