#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/evsids_heap.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("evsids heap tracks scored variables", "[sat]")
    {
        evsids_heap heap;
        const variable first {7u};
        const variable second {3u};

        heap.increase_score(first);
        heap.increase_score(first);
        heap.increase_score(second);

        REQUIRE(heap.contains(first));
        REQUIRE(heap.contains(second));
        REQUIRE(heap.extract_best().has_value());
    }

    TEST_CASE("evsids heap rescales scores before selecting the best variable", "[sat]")
    {
        evsids_heap heap;
        const variable first {7u};
        const variable second {3u};

        heap.increase_score(first);
        heap.increase_score(first);
        heap.increase_score(second);
        heap.rescale();
        heap.increase_score(second);
        heap.increase_score(second);

        const auto best = heap.extract_best();
        REQUIRE(best.has_value());
        REQUIRE(best->index() == second.index());
    }

    TEST_CASE("evsids heap reports no best variable when empty", "[sat]")
    {
        evsids_heap heap;

        REQUIRE(!heap.extract_best().has_value());
    }
}
