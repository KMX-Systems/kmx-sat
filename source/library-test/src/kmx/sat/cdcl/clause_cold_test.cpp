#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <kmx/sat/cdcl/store/clause_cold.hpp>

namespace kmx::sat::cdcl::store
{
    TEST_CASE("clause cold storage is disabled by default", "[sat]")
    {
        clause_cold cold;
        const clause::ref_t ref {17u};

        cold.demote_to_cold(ref);
        REQUIRE_FALSE(cold.enabled());
        REQUIRE_FALSE(cold.is_cold(ref));
        REQUIRE(cold.cold_footprint_bytes() == 0u);
    }

    TEST_CASE("clause cold storage tracks explicit opt-in lifecycle", "[sat]")
    {
        clause_cold cold;
        const clause::ref_t ref {23u};
        cold.set_enabled(true);

        cold.demote_to_cold(ref);
        cold.demote_to_cold(ref);
        REQUIRE(cold.is_cold(ref));
        REQUIRE(cold.cold_footprint_bytes() == 8u);

        cold.decode_on_access(ref);
        REQUIRE(cold.access_count() == 1u);
        REQUIRE(cold.promote_from_cold(ref) == ref);
        REQUIRE(cold.promotion_count() == 1u);
        REQUIRE_FALSE(cold.is_cold(ref));
        REQUIRE(cold.cold_footprint_bytes() == 0u);
    }

    TEST_CASE("clause cold storage round trips varint payloads", "[sat]")
    {
        clause_cold cold;
        const clause::ref_t ref {31u};
        const std::array<literal, 3> literals {literal {variable {100u}, true}, literal {variable {4u}, false},
                                               literal {variable {101u}, false}};
        cold.set_enabled(true);
        cold.demote_to_cold(ref, literals);

        const auto decoded = cold.decode_literals(ref);
        REQUIRE(decoded.size() == literals.size());
        REQUIRE(decoded[0].raw() == literals[0].raw());
        REQUIRE(decoded[1].raw() == literals[1].raw());
        REQUIRE(decoded[2].raw() == literals[2].raw());
        REQUIRE(cold.cold_footprint_bytes() > 0u);

        cold.promote_from_cold(ref);
        REQUIRE(cold.cold_footprint_bytes() == 0u);
        REQUIRE(cold.decode_literals(ref).empty());

        cold.demote_to_cold(ref, literals);
        cold.decode_on_access(ref);
        cold.reset();
        REQUIRE(cold.enabled());
        REQUIRE(cold.cold_footprint_bytes() == 0u);
        REQUIRE(cold.access_count() == 0u);
        REQUIRE(cold.promotion_count() == 0u);
        REQUIRE(cold.decode_literals(ref).empty());
    }

    TEST_CASE("clause cold storage rewrites tracked references after relocation", "[sat]")
    {
        clause_cold cold;
        const clause::ref_t old_ref {41u};
        const clause::ref_t new_ref {87u};
        const std::array<literal, 2> literals {literal {variable {9u}, false}, literal {variable {3u}, true}};
        cold.set_enabled(true);
        cold.demote_to_cold(old_ref, literals);

        cold.rewrite_ref_after_gc(old_ref, new_ref);

        REQUIRE_FALSE(cold.is_cold(old_ref));
        REQUIRE(cold.is_cold(new_ref));
        const auto decoded = cold.decode_literals(new_ref);
        REQUIRE(decoded.size() == literals.size());
        REQUIRE(decoded[0].raw() == literals[0].raw());
        REQUIRE(decoded[1].raw() == literals[1].raw());
    }

    TEST_CASE("clause cold storage reports a compressed footprint", "[sat]")
    {
        clause_cold cold;
        const clause::ref_t ref {91u};
        std::array<literal, 32> literals {};
        for (std::uint32_t index {}; index < literals.size(); ++index)
            literals[index] = literal {variable {index + 1u}, false};

        cold.set_enabled(true);
        cold.demote_to_cold(ref, literals);

        const auto raw_payload_bytes = literals.size() * sizeof(literal::raw_t);
        REQUIRE(cold.cold_footprint_bytes() < raw_payload_bytes);
        REQUIRE(cold.decode_literals(ref).size() == literals.size());
    }
}
