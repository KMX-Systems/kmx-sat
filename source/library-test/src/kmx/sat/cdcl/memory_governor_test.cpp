#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/memory_governor.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("memory governor tracks budget and escalation state", "[sat]")
    {
        memory_governor governor;

        REQUIRE(governor.current_usage() == 0u);
        REQUIRE(governor.soft_limit_breached() == false);
        REQUIRE(governor.hard_limit_breached() == false);

        governor.register_budget(10u, 20u);
        governor.request_shrink();
        governor.escalate_policy();

        REQUIRE(governor.current_usage() == 0u);
        REQUIRE(governor.soft_limit_breached() == false);
        REQUIRE(governor.hard_limit_breached() == false);
        REQUIRE(governor.shrink_requests() == 1u);
        REQUIRE(governor.escalation_steps() == 1u);

        governor.reset_epoch_usage();
        REQUIRE(governor.escalation_steps() == 1u);
    }
}
