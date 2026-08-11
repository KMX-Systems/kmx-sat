#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/garbage_collector.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("garbage collector tracks collection lifecycle", "[sat]")
    {
        garbage_collector collector;

        REQUIRE(collector.should_collect() == false);

        collector.collect();
        collector.relocate_live_clause();
        collector.rewrite_watchers();
        collector.rewrite_reasons();
        collector.finalize_cycle();

        REQUIRE(collector.relocated_clause_count() == 1u);
        REQUIRE(collector.watch_rewrite_count() == 1u);
        REQUIRE(collector.reason_rewrite_count() == 1u);
        REQUIRE(collector.finalized() == true);
        REQUIRE(collector.should_collect() == false);
    }
}
