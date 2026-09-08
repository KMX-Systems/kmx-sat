#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/model_reconstructor.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    namespace
    {
    }

    using dimacs_clause_t = std::vector<int>;

    /// @brief Evaluates a DIMACS-style formula under a truth-value table indexed by variable.
    static bool formula_is_satisfied(const std::vector<dimacs_clause_t>& formula, const std::vector<bool>& values) noexcept
    {
        for (const auto& clause: formula)
        {
            bool satisfied {};
            for (const auto dimacs_literal: clause)
            {
                const auto index = static_cast<std::size_t>((dimacs_literal < 0) ? -dimacs_literal : dimacs_literal);
                if ((index < values.size()) && (values[index] == (dimacs_literal > 0)))
                {
                    satisfied = true;
                    break;
                }
            }
            if (!satisfied)
                return false;
        }
        return true;
    }

    /// @brief Converts a DIMACS clause into solver literals.
    static std::vector<literal> to_literals(const dimacs_clause_t& clause) noexcept
    {
        std::vector<literal> literals {};
        literals.reserve(clause.size());
        for (const auto dimacs_literal: clause)
        {
            const auto index = static_cast<variable::index_t>((dimacs_literal < 0) ? -dimacs_literal : dimacs_literal);
            literals.push_back(literal {variable {index}, dimacs_literal < 0});
        }
        return literals;
    }

    TEST_CASE("model reconstruction restores eliminated variables", "[sat]")
    {
        SECTION("a variable resolved away is given a value that satisfies the original formula")
        {
            // Reconstruction is what makes bounded variable elimination sound: the search only ever sees the
            // reduced formula, so without replaying the extension stack an eliminated variable keeps whatever
            // value the reduced formula happened to leave it with, and the reported model can falsify a clause
            // that was removed. This drives real resolution-based eliminations and checks the restored model
            // against the pre-elimination formula.
            std::mt19937 rng {2026u};
            std::size_t reconstructions_checked {};

            for (int trial = 0; trial < 600; ++trial)
            {
                static constexpr int variable_count = 6;
                const auto clause_count = 8 + static_cast<int>(rng() % 8u);

                std::vector<dimacs_clause_t> formula {};
                for (int index = 0; index < clause_count; ++index)
                {
                    dimacs_clause_t clause {};
                    const auto width = 2 + static_cast<int>(rng() % 2u);
                    while (static_cast<int>(clause.size()) < width)
                    {
                        const auto candidate = 1 + static_cast<int>(rng() % variable_count);
                        const auto occurs = std::any_of(clause.begin(), clause.end(), [candidate](const int existing) noexcept
                                                        { return ((existing < 0) ? -existing : existing) == candidate; });
                        if (!occurs)
                            clause.push_back(((rng() & 1u) != 0u) ? candidate : -candidate);
                    }
                    formula.push_back(clause);
                }

                const auto eliminated = 1 + static_cast<int>(rng() % variable_count);

                std::vector<dimacs_clause_t> reduced {};
                std::vector<dimacs_clause_t> positive {};
                std::vector<dimacs_clause_t> negative {};
                std::vector<dimacs_clause_t> touching {};
                for (const auto& clause: formula)
                {
                    const auto has_positive = std::find(clause.begin(), clause.end(), eliminated) != clause.end();
                    const auto has_negative = std::find(clause.begin(), clause.end(), -eliminated) != clause.end();
                    if (has_positive)
                        positive.push_back(clause);
                    if (has_negative)
                        negative.push_back(clause);
                    if (has_positive || has_negative)
                        touching.push_back(clause);
                    else
                        reduced.push_back(clause);
                }

                for (const auto& left: positive)
                {
                    for (const auto& right: negative)
                    {
                        dimacs_clause_t resolvent {};
                        bool tautological {};
                        for (const auto dimacs_literal: left)
                            if ((dimacs_literal != eliminated) &&
                                (std::find(resolvent.begin(), resolvent.end(), dimacs_literal) == resolvent.end()))
                                resolvent.push_back(dimacs_literal);
                        for (const auto dimacs_literal: right)
                        {
                            if (dimacs_literal == -eliminated)
                                continue;
                            if (std::find(resolvent.begin(), resolvent.end(), -dimacs_literal) != resolvent.end())
                            {
                                tautological = true;
                                break;
                            }
                            if (std::find(resolvent.begin(), resolvent.end(), dimacs_literal) == resolvent.end())
                                resolvent.push_back(dimacs_literal);
                        }
                        if (!tautological && !resolvent.empty())
                            reduced.push_back(resolvent);
                    }
                }

                // Stand in for the search: any model of the reduced formula, with the eliminated variable left
                // at an arbitrary value, is what `solver_core` would hand to reconstruction.
                std::vector<bool> reduced_model {};
                bool found_reduced_model {};
                for (int mask = 0; (mask < (1 << variable_count)) && !found_reduced_model; ++mask)
                {
                    std::vector<bool> values(static_cast<std::size_t>(variable_count) + 1u, false);
                    for (int index = 1; index <= variable_count; ++index)
                        values[static_cast<std::size_t>(index)] = ((mask >> (index - 1)) & 1) != 0;
                    values[static_cast<std::size_t>(eliminated)] = false;
                    if (formula_is_satisfied(reduced, values))
                    {
                        reduced_model = values;
                        found_reduced_model = true;
                    }
                }
                if (!found_reduced_model)
                    continue;

                stack::extension extension_stack {};
                const auto mark = extension_stack.witness_mark();
                for (const auto& clause: touching)
                {
                    const auto literals = to_literals(clause);
                    extension_stack.append_witness_clause(std::span<const literal> {literals});
                }
                extension_stack.push_bve_elimination(variable {static_cast<variable::index_t>(eliminated)}, mark);

                std::vector<literal> initial_model {};
                for (int index = 1; index <= variable_count; ++index)
                    initial_model.push_back(
                        literal {variable {static_cast<variable::index_t>(index)}, !reduced_model[static_cast<std::size_t>(index)]});

                model_reconstructor reconstructor {};
                reconstructor.attach_extension_stack(extension_stack);
                reconstructor.set_initial_model(initial_model);
                const auto model = reconstructor.reconstruct_full_model();

                std::vector<bool> restored(static_cast<std::size_t>(variable_count) + 1u, false);
                for (const auto lit: model.values())
                    restored[static_cast<std::size_t>(lit.variable_of().index())] = !lit.is_negated();

                REQUIRE(formula_is_satisfied(formula, restored));
                ++reconstructions_checked;
            }

            REQUIRE(reconstructions_checked > 100u);
        }

        SECTION("internally introduced variables stay out of the exposed model")
        {
            stack::extension extension_stack {};
            extension_stack.push_factor_record(extension_record {factor_transformation {variable {3u}}});

            model_reconstructor reconstructor {};
            reconstructor.attach_extension_stack(extension_stack);
            reconstructor.set_initial_model(
                {literal {variable {1u}, false}, literal {variable {2u}, true}, literal {variable {3u}, false}});

            const auto model = reconstructor.reconstruct_full_model();
            const auto values = model.values();
            REQUIRE(values.size() == 2u);
            REQUIRE(std::none_of(values.begin(), values.end(), [](const literal lit) noexcept { return lit.variable_of().index() == 3u; }));
        }
    }
}
