#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/assumption_reuse_advisor.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("assumption reuse advisor retains and resets learned history", "[sat]")
    {
        assumption_reuse_advisor advisor;

        REQUIRE(advisor.has_suggested_order() == false);

        advisor.record_epoch_outcome();
        REQUIRE(advisor.suggest_trail_reuse_depth() > 0u);

        advisor.suggest_assumption_order();
        REQUIRE(advisor.has_suggested_order() == true);

        advisor.reset_history();
        REQUIRE(advisor.suggest_trail_reuse_depth() == 0u);
        REQUIRE(advisor.has_suggested_order() == false);
    }

    TEST_CASE("assumption reuse advisor caps trail reuse depth", "[sat]")
    {
        assumption_reuse_advisor advisor;

        for (std::uint32_t i = 0; i < 10u; ++i)
            advisor.record_epoch_outcome();

        REQUIRE(advisor.suggest_trail_reuse_depth() == 4u);
    }
}
