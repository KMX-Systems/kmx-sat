#include <catch2/catch_test_macros.hpp>


#include <kmx/sat/simplify/equivalence_substitutor.hpp>

namespace kmx::sat::simplify {

TEST_CASE("equivalence substitutor", "[sat]")
{
    using namespace kmx::sat::simplify;

    equivalence_substitutor substitutor;
    substitutor.apply_equivalence_class();
    substitutor.rewrite_clauses();
    substitutor.rewrite_watches();
    substitutor.rewrite_external_mapping();
    REQUIRE(substitutor.equivalence_class_count() == 1u);
    REQUIRE(substitutor.clause_rewrite_count() == 1u);
    REQUIRE(substitutor.watch_rewrite_count() == 1u);
    REQUIRE(substitutor.external_mapping_rewrite_count() == 1u);
    REQUIRE(substitutor.rewrite_completed());

    // removed std::cout: "equivalence substitutor test passed\n";
    }

} // namespace
