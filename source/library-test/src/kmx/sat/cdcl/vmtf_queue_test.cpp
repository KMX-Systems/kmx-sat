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

    TEST_CASE("vmtf queue shuffle perturbs the front candidate", "[sat]")
    {
        vmtf_queue queue;
        const variable first {1u};
        const variable second {2u};
        const variable third {3u};

        queue.activate(first);
        queue.activate(second);
        queue.activate(third);
        REQUIRE(queue.front_candidate()->index() == first.index());

        queue.shuffle();

        const auto candidate_after_shuffle = queue.front_candidate();
        REQUIRE(candidate_after_shuffle.has_value());
        REQUIRE(candidate_after_shuffle->index() == second.index());
        REQUIRE(queue.last_shuffle_stride() == 1u);

        queue.shuffle();
        const auto candidate_after_second_shuffle = queue.front_candidate();
        REQUIRE(candidate_after_second_shuffle.has_value());
        REQUIRE(candidate_after_second_shuffle->index() == first.index());
        REQUIRE(queue.last_shuffle_stride() == 2u);

        queue.shuffle(1u);
        const auto candidate_after_salted_shuffle = queue.front_candidate();
        REQUIRE(candidate_after_salted_shuffle.has_value());
        REQUIRE(candidate_after_salted_shuffle->index() == third.index());
        REQUIRE(queue.last_shuffle_stride() == 2u);
    }
}
