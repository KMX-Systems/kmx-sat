#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/simplify/equivalence_substitutor.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{

    TEST_CASE("equivalence substitutor", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::cdcl;
        using namespace kmx::sat::simplify;

        clause::database clause_database;
        bank::watch_list watch_list;
        variable_mapper mapper;

        const variable var_three {3u};
        const variable var_five {5u};
        const literal lit_three_pos {var_three, false};
        const literal lit_three_neg {var_three, true};
        const literal lit_five_pos {var_five, false};
        const literal lit_five_neg {var_five, true};

        const std::array<literal, 2u> irredundant_clause {lit_five_pos, lit_three_neg};
        const std::array<literal, 2u> redundant_clause {lit_five_neg, lit_three_pos};
        const auto irredundant_ref = clause_database.add_clause(irredundant_clause, false);
        const auto redundant_ref = clause_database.add_clause(redundant_clause, true);

        // A binary watch carries the clause's other literal as its blocking literal; there is no second field.
        watch binary_watch {lit_five_neg, redundant_ref, true};
        watch_list.watch_literal(lit_five_pos, binary_watch);

        const variable external_var {9u};
        const variable internal_var = mapper.ensure_external_variable(external_var);

        equivalence_substitutor substitutor;
        substitutor.attach_clause_database(clause_database);
        substitutor.attach_watch_list(watch_list);
        substitutor.attach_variable_mapper(mapper);
        REQUIRE(!substitutor.rewrite_completed());

        substitutor.apply_equivalence_class(var_five.index(), var_three.index());
        REQUIRE(substitutor.pending_rewrite_count() == 1u);

        const variable remapped_internal_target {internal_var.index() + 17u};
        substitutor.apply_equivalence_class(internal_var.index(), remapped_internal_target.index());
        REQUIRE(substitutor.pending_rewrite_count() == 2u);

        substitutor.rewrite_clauses();
        substitutor.rewrite_watches();
        REQUIRE(substitutor.pending_rewrite_count() == 2u);

        substitutor.rewrite_external_mapping();

        const auto rewritten_irredundant = clause_database.storage_of().literals_of(irredundant_ref);
        REQUIRE(rewritten_irredundant.size() == 2u);
        REQUIRE(rewritten_irredundant[0u] == lit_three_pos);
        REQUIRE(rewritten_irredundant[1u] == lit_three_neg);

        const auto rewritten_redundant = clause_database.storage_of().literals_of(redundant_ref);
        REQUIRE(rewritten_redundant.size() == 2u);
        REQUIRE(rewritten_redundant[0u] == lit_three_neg);
        REQUIRE(rewritten_redundant[1u] == lit_three_pos);

        REQUIRE(watch_list.size_of(lit_five_pos) == 0u);
        REQUIRE(watch_list.size_of(lit_three_pos) == 1u);

        std::vector<watch> remapped_watches {};
        watch_list.iterate(lit_three_pos, [&](const watch& entry) noexcept { remapped_watches.push_back(entry); });
        REQUIRE(remapped_watches.size() == 1u);
        REQUIRE(remapped_watches[0u].clause_ref() == redundant_ref);
        REQUIRE(remapped_watches[0u].blocking_literal() == lit_three_neg);
        REQUIRE(remapped_watches[0u].binary_literal() == lit_three_neg);

        const auto remapped_internal = mapper.to_internal_literal(literal {external_var, false});
        REQUIRE(remapped_internal.variable_of().index() == remapped_internal_target.index());

        REQUIRE(substitutor.equivalence_class_count() == 2u);
        REQUIRE(substitutor.clause_rewrite_count() == 1u);
        REQUIRE(substitutor.watch_rewrite_count() == 1u);
        REQUIRE(substitutor.external_mapping_rewrite_count() == 1u);
        REQUIRE(substitutor.last_applied_rewrite_count() == 2u);
        REQUIRE(substitutor.pending_rewrite_count() == 0u);
        REQUIRE(substitutor.rewrite_completed());

        // removed std::cout: "equivalence substitutor test passed\n";
    }

} // namespace
