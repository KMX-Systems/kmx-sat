#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

#include <kmx/sat/cdcl/clause/minimizer.hpp>
#include <kmx/sat/cdcl/clause/storage.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("clause minimizer", "[sat]")
    {
        using namespace kmx::sat;

        clause::database database;
        auto& storage = database.storage_of();
        clause::minimizer minimizer;
        minimizer.attach_storage(storage);
        minimizer.attach_database(database);

        const variable var_a {1u};
        const variable var_b {2u};
        const literal a_pos {var_a, false};
        const literal b_pos {var_b, false};

        const std::array<literal, 3u> literals {a_pos, b_pos, a_pos};
        const auto ref = database.add_clause(literals, true);
        REQUIRE(ref.valid());
        REQUIRE(database.tier_of(ref) == clause::database::default_tier);

        minimizer.minimize_learned_clause(ref);
        minimizer.shrink_clause(ref);
        minimizer.recompute_glue(ref);
        minimizer.promote_if_needed(ref);

        const auto stored_literals = storage.literals_of(ref);
        REQUIRE(stored_literals.size() == 2u);
        REQUIRE(minimizer.minimized_clause_count() == 1u);
        REQUIRE(minimizer.shrunk_clause_count() == 1u);
        REQUIRE(minimizer.last_glue() == 2u);
        REQUIRE(minimizer.promoted_clause_count() == 1u);
        REQUIRE(database.tier_of(ref) == 0u);

        const std::array<literal, 3u> lbd_literals {literal {variable {20u}, false}, literal {variable {21u}, false},
                                                   literal {variable {22u}, false}};
        const auto lbd_ref = storage.create_learned_clause(lbd_literals);
        const auto level_of = [](const void*, const variable var) noexcept -> std::uint32_t { return (var.index() == 22u) ? 2u : 1u; };

        minimizer.recompute_glue(lbd_ref, level_of, nullptr);
        REQUIRE(minimizer.last_glue() == 2u);

        const std::array<literal, 3u> closure_literals {literal {variable {30u}, false}, literal {variable {31u}, false},
                                                       literal {variable {32u}, false}};
        const auto closure_ref = storage.create_learned_clause(closure_literals);
        const std::array<literal, 2u> reason_for_31 {literal {variable {31u}, false}, literal {variable {32u}, false}};
        const auto reason_of = [](const void* context, const variable var) noexcept -> std::span<const literal>
        {
            if (var.index() != 31u)
                return {};
            return *static_cast<const std::array<literal, 2u>*>(context);
        };

        minimizer.minimize_learned_clause(closure_ref, level_of, reason_of, &reason_for_31);
        minimizer.shrink_clause(closure_ref);
        const auto minimized_literals = storage.literals_of(closure_ref);
        REQUIRE(minimized_literals.size() == 2u);
        REQUIRE(minimized_literals[0u].variable_of().index() == 30u);
        REQUIRE(minimized_literals[1u].variable_of().index() == 32u);
    }
}
