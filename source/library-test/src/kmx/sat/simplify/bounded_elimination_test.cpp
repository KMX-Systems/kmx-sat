#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/model_reconstructor.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    using dimacs_clause = std::vector<int>;

    static bool formula_is_satisfied(const std::vector<dimacs_clause>& formula, const std::vector<bool>& values) noexcept
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

    static bool find_any_model(const std::vector<dimacs_clause>& formula, const int variable_count,
                               std::vector<bool>& model) noexcept
    {
        for (int mask = 0; mask < (1 << variable_count); ++mask)
        {
            std::vector<bool> values(static_cast<std::size_t>(variable_count) + 1u, false);
            for (int index = 1; index <= variable_count; ++index)
                values[static_cast<std::size_t>(index)] = ((mask >> (index - 1)) & 1) != 0;
            if (formula_is_satisfied(formula, values))
            {
                model = values;
                return true;
            }
        }
        return false;
    }

    TEST_CASE("bounded variable elimination preserves satisfiability and reconstructs models", "[sat]")
    {
        // The two properties that make BVE safe to enable at all. Satisfiability equivalence alone is not enough:
        // the search only ever sees the reduced formula, so a reported model must still be repairable into a model
        // of the original, which is what the extension stack and `model_reconstructor` exist for.
        std::mt19937 rng {7u};
        std::size_t formulas_with_eliminations = 0u;
        std::size_t variables_eliminated = 0u;

        for (int trial = 0; trial < 400; ++trial)
        {
            static constexpr int variable_count = 8;
            const auto clause_count = 10 + static_cast<int>(rng() % 12u);

            std::vector<dimacs_clause> formula {};
            for (int index = 0; index < clause_count; ++index)
            {
                dimacs_clause clause {};
                const auto width = 2 + static_cast<int>(rng() % 2u);
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
            cdcl::stack::extension extension_stack {};
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

            eliminator::variable::bounded eliminator {};
            eliminator.attach_clause_database(database);
            eliminator.attach_extension_stack(extension_stack);
            eliminator.run();

            if (eliminator.eliminated_variables().empty())
                continue;
            ++formulas_with_eliminations;
            variables_eliminated += eliminator.eliminated_variables().size();

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

            std::vector<bool> original_model {};
            std::vector<bool> reduced_model {};
            const auto original_satisfiable = find_any_model(formula, variable_count, original_model);
            const auto reduced_satisfiable = find_any_model(reduced, variable_count, reduced_model);
            REQUIRE(original_satisfiable == reduced_satisfiable);

            if (!reduced_satisfiable)
                continue;

            std::vector<literal> initial_model {};
            for (int index = 1; index <= variable_count; ++index)
                initial_model.push_back(literal {variable {static_cast<variable::index_t>(index)},
                                                 !reduced_model[static_cast<std::size_t>(index)]});

            cdcl::model_reconstructor reconstructor {};
            reconstructor.attach_extension_stack(extension_stack);
            reconstructor.set_initial_model(initial_model);
            const auto model = reconstructor.reconstruct_full_model();

            std::vector<bool> restored(static_cast<std::size_t>(variable_count) + 1u, false);
            for (const auto lit: model.values())
                restored[static_cast<std::size_t>(lit.variable_of().index())] = !lit.is_negated();

            REQUIRE(formula_is_satisfied(formula, restored));
        }

        REQUIRE(formulas_with_eliminations > 100u);
        REQUIRE(variables_eliminated > 100u);
    }
}
