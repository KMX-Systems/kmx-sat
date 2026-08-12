#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/incremental_context.hpp>

namespace kmx::sat::cdcl
{

    TEST_CASE("incremental context", "[sat]")
    {
        using namespace kmx::sat::cdcl;

        incremental_context context;
        context.begin_solve_epoch();
        REQUIRE(context.in_epoch());
        context.retain_learned_clause();
        context.retain_learned_clause();
        context.end_solve_epoch();
        REQUIRE(!context.in_epoch());
        REQUIRE(context.retained_learned_clauses() == 2u);
        REQUIRE(context.last_epoch_retained_learned_clauses() == 2u);

        context.reset_transient_state();
        REQUIRE(context.transient_state_reset());
        REQUIRE(context.transient_reset_count() == 1u);

        context.persist_option_subset();
        REQUIRE(context.persisted_option_subset());
        // removed std::cout: "incremental context test passed\n";
    }

} // namespace
