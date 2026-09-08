#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/checker/lrat.hpp>
#include <kmx/sat/proof/checker/online.hpp>
#include <kmx/sat/proof/clause/id.hpp>

namespace kmx::sat::proof
{

    TEST_CASE("checker", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::proof;

        // checker::online: structural add/delete/shrink bookkeeping.
        {
            checker::online online_checker;
            const cdcl::clause::ref_t ref_1 {1u};
            const cdcl::clause::ref_t ref_2 {2u};

            online_checker.on_add(ref_1);
            online_checker.on_add(ref_2);
            online_checker.on_shrink(ref_1);
            online_checker.on_delete(ref_2);
            REQUIRE(online_checker.validate_conclusion());
            REQUIRE(online_checker.coverage_snapshot().clauses_added == 2u);
            REQUIRE(online_checker.coverage_snapshot().clauses_deleted == 1u);
            REQUIRE(online_checker.coverage_snapshot().clauses_shrunk == 1u);

            // Deleting an already-deleted/unknown reference is a structural error.
            online_checker.on_delete(ref_2);
            REQUIRE(!online_checker.validate_conclusion());
            REQUIRE(online_checker.coverage_snapshot().structural_errors == 1u);
        }

        // checker::lrat: reverse unit propagation over a recorded antecedent chain.
        {
            checker::lrat lrat_checker;
            const variable var_a {1u};
            const variable var_b {2u};
            const variable var_c {3u};
            const literal a_pos {var_a, false};
            const literal a_neg {var_a, true};
            const literal b_pos {var_b, false};
            const literal b_neg {var_b, true};
            const literal c_pos {var_c, false};
            const literal c_neg {var_c, true};

            // Original clauses: (a v b), (-a v c), (-c) -- unsatisfiable together with (-b).
            const clause::id id_1 {1u};
            const clause::id id_2 {2u};
            const clause::id id_3 {3u};
            const clause::id id_4 {4u}; // derived unit: b (from clause_bc and id_3 resolved on c)
            const clause::id id_5 {5u}; // derived empty clause

            const std::array<literal, 2u> clause_1 {a_pos, b_pos};
            const std::array<literal, 2u> clause_2 {a_neg, c_pos};
            const std::array<literal, 1u> clause_3 {c_neg};
            lrat_checker.record_clause(id_1, clause_1);
            lrat_checker.record_clause(id_2, clause_2);
            lrat_checker.record_clause(id_3, clause_3);

            // Derive (b v c) from clause_1 and clause_2 by resolving on `a`.
            const clause::id id_bc {6u};
            const std::array<literal, 2u> clause_bc {b_pos, c_pos};
            lrat_checker.record_clause(id_bc, clause_bc);
            const std::array<clause::id, 2u> chain_bc {id_1, id_2};
            lrat_checker.record_antecedents(id_bc, chain_bc);
            REQUIRE(lrat_checker.validate_clause(id_bc));

            // Derive unit (b) from (b v c) and clause_3 by resolving on `c`.
            const std::array<literal, 1u> clause_b {b_pos};
            lrat_checker.record_clause(id_4, clause_b);
            const std::array<clause::id, 2u> chain_b {id_bc, id_3};
            lrat_checker.record_antecedents(id_4, chain_b);
            REQUIRE(lrat_checker.validate_clause(id_4));

            // Wrong chain: b cannot be derived from clause_1 and clause_3 alone.
            const std::array<clause::id, 2u> bad_chain {id_1, id_3};
            lrat_checker.record_antecedents(id_4, bad_chain);
            REQUIRE(!lrat_checker.validate_clause(id_4));
            lrat_checker.record_antecedents(id_4, chain_b);

            // Derive the empty clause from unit (b), clause_3, and unit (-b) is not available;
            // instead resolve (b) with (-b v -c) then with a fact asserting c to reach empty.
            // Simpler: derive empty clause directly from id_4 (b) and a recorded unit (-b).
            const clause::id id_notb {7u};
            const std::array<literal, 1u> clause_notb {b_neg};
            lrat_checker.record_clause(id_notb, clause_notb);

            const std::array<literal, 0u> empty_clause {};
            lrat_checker.record_clause(id_5, empty_clause);
            const std::array<clause::id, 2u> chain_empty {id_4, id_notb};
            lrat_checker.record_antecedents(id_5, chain_empty);
            REQUIRE(lrat_checker.validate_clause(id_5));
            REQUIRE(lrat_checker.finalize_unsat());
            REQUIRE(lrat_checker.check_chain());

            lrat_checker.forget_clause(id_5);
            REQUIRE(!lrat_checker.finalize_unsat());
        }

        // removed std::cout: "checker test passed\n";
    }

} // namespace
