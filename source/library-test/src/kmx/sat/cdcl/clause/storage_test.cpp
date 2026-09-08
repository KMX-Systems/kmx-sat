#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/clause/storage.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{

    TEST_CASE("storage", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::cdcl;

        clause::storage storage;

        const variable var_a {1u};
        const variable var_b {2u};
        const variable var_c {3u};
        const literal a_pos {var_a, false};
        const literal b_neg {var_b, true};
        const literal c_pos {var_c, false};

        // Original clause round-trips its literals and receives a stable proof id.
        const std::array<literal, 2u> original_literals {a_pos, b_neg};
        const auto original_ref = storage.create_original_clause(original_literals);
        REQUIRE(original_ref.valid());
        REQUIRE(!storage.is_redundant(original_ref));

        const auto original_literals_readback = storage.literals_of(original_ref);
        REQUIRE(original_literals_readback.size() == 2u);
        REQUIRE(original_literals_readback[0u] == a_pos);
        REQUIRE(original_literals_readback[1u] == b_neg);

        const auto original_proof_id = storage.proof_id_of(original_ref);
        REQUIRE(original_proof_id.valid());

        // Learned clause is tracked as redundant and gets a distinct proof id.
        const std::array<literal, 3u> learned_literals {a_pos, b_neg, c_pos};
        const auto learned_ref = storage.create_learned_clause(learned_literals);
        REQUIRE(learned_ref.valid());
        REQUIRE(storage.is_redundant(learned_ref));
        REQUIRE(!storage.proof_id_of(learned_ref).equals(original_proof_id));

        const auto learned_literals_readback = storage.literals_of(learned_ref);
        REQUIRE(learned_literals_readback.size() == 3u);

        // Shrinking truncates the stored literal payload in place.
        storage.shrink_clause(learned_ref, 2u);
        const auto shrunk_literals = storage.literals_of(learned_ref);
        REQUIRE(shrunk_literals.size() == 2u);
        REQUIRE(shrunk_literals[0u] == a_pos);
        REQUIRE(shrunk_literals[1u] == b_neg);

        // Relocating preserves clause payload and proof identity under a new physical reference.
        const auto relocated_learned_ref = storage.relocate_clause(learned_ref);
        REQUIRE(relocated_learned_ref.valid());
        REQUIRE(relocated_learned_ref != learned_ref);
        REQUIRE(storage.resolve_ref(learned_ref) == relocated_learned_ref);
        REQUIRE(storage.proof_id_of(relocated_learned_ref).valid());
        REQUIRE(storage.proof_id_of(relocated_learned_ref).equals(storage.proof_id_of(storage.resolve_ref(learned_ref))));
        REQUIRE(storage.is_redundant(relocated_learned_ref));

        const auto relocated_literals = storage.literals_of(relocated_learned_ref);
        REQUIRE(relocated_literals.size() == 2u);
        REQUIRE(relocated_literals[0u] == a_pos);
        REQUIRE(relocated_literals[1u] == b_neg);

        // Destroying a clause retires its proof id.
        storage.destroy_clause(original_ref);
        REQUIRE(!storage.proof_id_of(original_ref).valid());

        const auto reallocated_ref = storage.create_original_clause(original_literals);
        REQUIRE(reallocated_ref.valid());
        REQUIRE(reallocated_ref != original_ref);
        REQUIRE(!storage.is_redundant(reallocated_ref));
        REQUIRE(storage.is_alive(reallocated_ref));

        // removed std::cout: "clause storage test passed\n";
    }

} // namespace
