#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/vmtf_queue.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("vmtf queue exposes the most recently bumped variable", "[sat]")
    {
        vmtf_queue queue;
        const variable first {4u};
        const variable second {7u};

        queue.activate(first);
        queue.activate(second);
        queue.bump(first);

        const auto candidate = queue.front_candidate();
        REQUIRE(candidate.has_value());
        REQUIRE(candidate->index() == first.index());
        REQUIRE(queue.size() == 2u);
    }
}
