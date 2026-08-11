#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/io/fixture/manifest.hpp>

namespace kmx::sat::io::fixture
{
    TEST_CASE("manifest tracks provenance completeness", "[sat]")
    {
        manifest value;
        REQUIRE_FALSE(value.has_provenance());

        value.set_source_origin("benchmarks/sample.cnf");
        REQUIRE(value.has_provenance());

        value.set_build_fingerprint("kmx-sat-2026.08.10");
        REQUIRE(value.has_provenance());
    }
}
