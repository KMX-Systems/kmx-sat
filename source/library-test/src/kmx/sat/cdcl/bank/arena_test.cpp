#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/bank/arena.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("arena materializes and swaps survivor storage", "[sat]")
    {
        bank::arena arena;

        const literal first {variable {1u}, false};
        const literal second {variable {2u}, true};
        const std::array<literal, 2> literals {first, second};

        const auto ref = arena.allocate_clause(literals.size());
        arena.write_literals(ref, literals);

        REQUIRE(arena.contains(ref));
        REQUIRE(arena.literal_count(ref) == 2u);

        arena.prepare_gc();
        arena.materialize_clause(ref);
        arena.swap_survivor();

        REQUIRE(arena.contains(ref));
        REQUIRE(arena.literal_count(ref) == 2u);

        const auto restored = arena.read_literals(ref);
        REQUIRE(restored.size() == 2u);
        REQUIRE(restored[0] == first);
        REQUIRE(restored[1] == second);
    }
}
