#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/simplify/scheduler/preprocess.hpp>

namespace kmx::sat::simplify
{
    TEST_CASE("preprocess scheduler runs enabled passes and reports summaries", "[sat]")
    {
        scheduler::preprocess scheduler;

        REQUIRE(!scheduler.should_abort_pipeline());
        REQUIRE(scheduler.enabled_pass_count() > 0u);

        scheduler.disable_pass("vivifier");
        const auto enabled_after_disable = scheduler.enabled_pass_count();
        REQUIRE(enabled_after_disable > 0u);

        scheduler.run_initial_pipeline();
        scheduler.report_pass_summary();

        REQUIRE(scheduler.pipeline_run_count() == 1u);
        REQUIRE(scheduler.executed_pass_count() == enabled_after_disable);
        REQUIRE(scheduler.reported_summary_count() == 1u);

        scheduler.enable_pass("vivifier");
        REQUIRE(scheduler.enabled_pass_count() == enabled_after_disable + 1u);
    }
}