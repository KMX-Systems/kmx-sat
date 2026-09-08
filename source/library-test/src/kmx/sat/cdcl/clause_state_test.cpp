#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/bank/arena.hpp>
#include <kmx/sat/cdcl/clause/header.hpp>
#include <kmx/sat/cdcl/clause/view.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{

    TEST_CASE("clause state", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::cdcl;

        bank::arena arena;
        const auto ref = arena.allocate_clause(2u);
        REQUIRE(ref.valid());
        REQUIRE(arena.contains(ref));

        watch watch_entry {literal {variable {1u}, false}, ref, true};
        watch_entry.set_binary_literal(literal {variable {2u}, true});
        REQUIRE(watch_entry.is_binary());
        REQUIRE(watch_entry.clause_ref().valid());

        clause::header header {2u, 1u, true, false, false, true, 0u, 3u};
        literal lits[] {literal {variable {1u}, false}, literal {variable {2u}, true}};
        clause::view view {lits, header};
        REQUIRE(view.size() == 2u);
        REQUIRE(view.is_binary());
        REQUIRE(view.contains(literal {variable {2u}, true}));
        REQUIRE(view.is_redundant());
        REQUIRE(view.is_satisfied_by_shrink());
        REQUIRE(view.header_data().used_count() == 3u);
        REQUIRE(view.header_data().tier() == 0u);

        // removed std::cout: "clause state test passed\n";
    }

    TEST_CASE("clause header mutators", "[sat]")
    {
        clause::header header {2u, 1u, false, false, false, false, 0u, 1u};
        header.set_redundant(true);
        header.set_garbage(true);
        header.set_reason(true);
        header.set_shrunken(true);
        header.set_tier(2u);
        header.increment_used_count();

        REQUIRE(header.is_redundant());
        REQUIRE(header.garbage());
        REQUIRE(header.is_active_reason());
        REQUIRE(header.is_satisfied_by_shrink());
        REQUIRE(header.tier() == 2u);
        REQUIRE(header.used_count() == 2u);

        header.set_reason(false);
        REQUIRE_FALSE(header.is_active_reason());
    }

} // namespace
