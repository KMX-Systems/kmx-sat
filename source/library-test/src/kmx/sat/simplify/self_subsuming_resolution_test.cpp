#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    using dimacs_clause = std::vector<int>;

    static bool clauses_are_satisfied(const std::vector<dimacs_clause>& formula, const std::vector<bool>& values) noexcept
    {
        for (const auto& clause: formula)
        {
            const auto satisfied = std::any_of(clause.begin(), clause.end(),
                                               [&values](const int dimacs_literal) noexcept
                                               {
                                                   const auto index =
                                                       static_cast<std::size_t>(dimacs_literal < 0 ? -dimacs_literal : dimacs_literal);
                                                   return index < values.size() && values[index] == (dimacs_literal > 0);
                                               });
            if (!satisfied)
                return false;
        }
        return true;
    }

    TEST_CASE("self-subsuming resolution preserves every model", "[sat]")
    {
        // Strengthening removes a literal, which makes a clause harder to satisfy. That is only sound when a
        // resolution partner justifies it, and the property to hold is stronger than for elimination: no model
        // reconstruction happens afterwards, so every model of the strengthened formula must already satisfy the
        // original one.
        std::mt19937 rng {99u};
        std::size_t formulas_strengthened = 0u;
        std::size_t literals_removed = 0u;

        for (int trial = 0; trial < 300; ++trial)
        {
            static constexpr int variable_count = 8;
            const auto clause_count = 8 + static_cast<int>(rng() % 10u);

            std::vector<dimacs_clause> formula {};
            for (int index = 0; index < clause_count; ++index)
            {
                dimacs_clause clause {};
                const auto width = 2 + static_cast<int>(rng() % 3u);
                while (static_cast<int>(clause.size()) < width)
                {
                    const auto candidate = 1 + static_cast<int>(rng() % variable_count);
                    const auto occurs = std::any_of(clause.begin(), clause.end(), [candidate](const int existing) noexcept
                                                    { return (existing < 0 ? -existing : existing) == candidate; });
                    if (!occurs)
                        clause.push_back((rng() & 1u) != 0u ? candidate : -candidate);
                }
                formula.push_back(clause);
            }

            cdcl::clause::database database {};
            for (const auto& clause: formula)
            {
                std::vector<literal> literals {};
                literals.reserve(clause.size());
                for (const auto dimacs_literal: clause)
                    literals.push_back(literal {variable {static_cast<variable::index_t>(dimacs_literal < 0 ? -dimacs_literal
                                                                                                            : dimacs_literal)},
                                                dimacs_literal < 0});
                database.add_clause(std::span<const literal> {literals}, false);
            }

            forward_subsumer subsumer {};
            subsumer.attach_database(database);
            REQUIRE_FALSE(subsumer.self_subsuming_resolution_enabled());
            subsumer.set_self_subsuming_resolution_enabled(true);
            subsumer.run();

            if (subsumer.strengthened_count() == 0u)
                continue;
            ++formulas_strengthened;
            literals_removed += subsumer.strengthened_count();

            std::vector<dimacs_clause> reduced {};
            const auto& storage = database.storage_of();
            database.iterate_irredundant(
                [&](const cdcl::clause::ref_t ref) noexcept
                {
                    if (database.is_garbage(ref))
                        return;
                    dimacs_clause clause {};
                    for (const auto lit: storage.view_literals(ref))
                    {
                        const auto index = static_cast<int>(lit.variable_of().index());
                        clause.push_back(lit.is_negated() ? -index : index);
                    }
                    reduced.push_back(clause);
                });

            auto original_satisfiable = false;
            auto reduced_satisfiable = false;
            for (int mask = 0; mask < (1 << variable_count); ++mask)
            {
                std::vector<bool> values(static_cast<std::size_t>(variable_count) + 1u, false);
                for (int index = 1; index <= variable_count; ++index)
                    values[static_cast<std::size_t>(index)] = ((mask >> (index - 1)) & 1) != 0;

                const auto satisfies_original = clauses_are_satisfied(formula, values);
                const auto satisfies_reduced = clauses_are_satisfied(reduced, values);
                original_satisfiable = original_satisfiable || satisfies_original;
                reduced_satisfiable = reduced_satisfiable || satisfies_reduced;

                // Strengthening may drop models, but never gain one that fails the original formula.
                REQUIRE_FALSE((satisfies_reduced && !satisfies_original));
            }
            REQUIRE(original_satisfiable == reduced_satisfiable);
        }

        REQUIRE(formulas_strengthened > 50u);
        REQUIRE(literals_removed > 100u);
    }
}
