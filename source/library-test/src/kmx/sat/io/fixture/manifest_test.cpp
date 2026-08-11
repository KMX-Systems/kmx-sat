#include <catch2/catch_test_macros.hpp>


#include <kmx/sat/io/fixture/manifest.hpp>

namespace kmx::sat::io::fixture {

TEST_CASE("manifest", "[sat]")
{
    using namespace kmx::sat::io::fixture;

    manifest value;
    value.set_fixture_id(42u);
    value.set_source_origin("benchmarks/sample.cnf");
    value.set_normalization_profile("dedup-tautology-drop");
    value.set_build_fingerprint("kmx-sat-2026.08.10");
    value.set_toolchain_fingerprint("gcc-14.2");

    REQUIRE(value.fixture_id() == 42u);
    REQUIRE(value.source_origin() == "benchmarks/sample.cnf");
    REQUIRE(value.normalization_profile() == "dedup-tautology-drop");
    REQUIRE(value.build_fingerprint() == "kmx-sat-2026.08.10");
    REQUIRE(value.toolchain_fingerprint() == "gcc-14.2");

    // removed std::cout: "fixture manifest test passed\n";
    }

} // namespace
