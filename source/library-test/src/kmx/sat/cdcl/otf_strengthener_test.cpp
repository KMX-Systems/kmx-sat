#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/otf_strengthener.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("otf strengthener", "[sat]")
    {
        otf_strengthener strengthener;
        const clause::ref_t ref {7u};

        REQUIRE(strengthener.try_strengthen(ref) == true);
        REQUIRE(strengthener.try_subsume(ref) == true);
        strengthener.rewrite_reason_if_needed(ref);
        strengthener.emit_proof_events();

        REQUIRE(strengthener.strengthened_clause_count() == 1u);
        REQUIRE(strengthener.subsumed_clause_count() == 1u);
        REQUIRE(strengthener.rewritten_reason_count() == 1u);
    }
}
