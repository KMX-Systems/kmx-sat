#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl {

TEST_CASE("database", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::cdcl;

    clause::database database;

    const variable var_a {1};
    const variable var_b {2};
    const variable var_c {3};
    const literal a_pos {var_a, false};
    const literal b_pos {var_b, false};
    const literal c_pos {var_c, false};

    const std::array<literal, 2> original_literals {a_pos, b_pos};
    const auto original_ref = database.add_clause(original_literals);
    REQUIRE(original_ref.valid());
    REQUIRE(database.tier_of(original_ref) == 0u);

    const std::array<literal, 2> learned_literals {b_pos, c_pos};
    const auto learned_ref = database.add_clause(learned_literals, true);
    REQUIRE(learned_ref.valid());
    REQUIRE(database.tier_of(learned_ref) == clause::database::default_tier);
    REQUIRE(database.storage_of().is_redundant(learned_ref));
    REQUIRE(!database.storage_of().is_redundant(original_ref));

    // Tier movement is clamped at both ends.
    database.promote_clause(learned_ref);
    REQUIRE(database.tier_of(learned_ref) == clause::database::default_tier - 1u);
    database.promote_clause(learned_ref);
    database.promote_clause(learned_ref);
    REQUIRE(database.tier_of(learned_ref) == 0u);

    database.demote_clause(original_ref);
    REQUIRE(database.tier_of(original_ref) == 1u);

    // Reason/garbage bookkeeping.
    database.mark_reason_clause(original_ref);
    REQUIRE(database.is_reason_clause(original_ref));
    database.mark_garbage(learned_ref);
    REQUIRE(database.is_garbage(learned_ref));

    // iterate_irredundant/iterate_redundant skip garbage-marked clauses.
    std::vector<cdcl::clause::ref_t> visited_irredundant {};
    database.iterate_irredundant([&](const cdcl::clause::ref_t ref) noexcept { visited_irredundant.push_back(ref); });
    REQUIRE(visited_irredundant.size() == 1);
    REQUIRE(visited_irredundant[0] == original_ref);

    std::vector<cdcl::clause::ref_t> visited_redundant {};
    database.iterate_redundant([&](const cdcl::clause::ref_t ref) noexcept { visited_redundant.push_back(ref); });
    REQUIRE(visited_redundant.empty()); // learned_ref is garbage-marked, so it is skipped.

    const auto stats_before = database.stats_snapshot();
    REQUIRE(stats_before.irredundant_count == 1);
    REQUIRE(stats_before.redundant_count == 1);
    REQUIRE(stats_before.garbage_count == 1);

    // flush_satisfied physically removes matching clauses via storage and clears their bookkeeping.
    database.flush_satisfied([&](const cdcl::clause::ref_t ref) noexcept { return ref == learned_ref; });
    const auto stats_after = database.stats_snapshot();
    REQUIRE(stats_after.redundant_count == 0);
    REQUIRE(!database.is_garbage(learned_ref));
    REQUIRE(!database.storage_of().proof_id_of(learned_ref).valid());

    // removed std::cout: "clause database test passed\n";
    }

} // namespace
