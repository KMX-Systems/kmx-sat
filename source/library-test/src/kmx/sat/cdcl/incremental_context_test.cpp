#include <catch2/catch_test_macros.hpp>


#include <kmx/sat/cdcl/incremental_context.hpp>

namespace kmx::sat::cdcl {

TEST_CASE("incremental context", "[sat]")
{
    using namespace kmx::sat::cdcl;

    incremental_context context;
    context.begin_solve_epoch();
    REQUIRE(context.in_epoch());
    context.retain_learned_clause();
    context.end_solve_epoch();
    REQUIRE(!context.in_epoch());
    context.reset_transient_state();
    context.persist_option_subset();
    // removed std::cout: "incremental context test passed\n";
    }

} // namespace
