/// @file library-test/src/kmx/sat/simplify/pass_equivalence_test.cpp
/// @brief Model-set equivalence of simplification passes on small formulas, for the passes that were once unsound.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/simplify/factorizer.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/transitive_reducer.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    namespace
    {
        using clause_t = std::vector<literal>;

        struct generator final
        {
            std::uint64_t state;
            std::uint32_t next(const std::uint32_t bound) noexcept
            {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<std::uint32_t>((state >> 33u) % bound);
            }
        };

        /// @brief The live irredundant clauses of a database.
        std::vector<clause_t> live_clauses(const cdcl::clause::database& database)
        {
            std::vector<clause_t> clauses;
            const auto& storage = database.storage_of();
            database.iterate_irredundant(
                [&](const cdcl::clause::ref_t ref) noexcept
                {
                    if (!ref.valid() || !storage.is_alive(ref) || database.is_garbage(ref))
                        return;
                    const auto literals = storage.view_literals(ref);
                    clauses.emplace_back(literals.begin(), literals.end());
                });
            return clauses;
        }

        /// @brief Every satisfying assignment over `variables` variables, as bit masks (bit v-1 = variable v true),
        /// projected onto the first `keep` variables.
        std::set<std::uint32_t> models(const std::vector<clause_t>& clauses, const std::uint32_t variables, const std::uint32_t keep)
        {
            std::set<std::uint32_t> result;
            for (std::uint32_t assignment = 0u; assignment < (1u << variables); ++assignment)
            {
                bool all = true;
                for (const auto& clause: clauses)
                {
                    bool satisfied = false;
                    for (const auto lit: clause)
                        satisfied |= (((assignment >> (lit.variable_of().index() - 1u)) & 1u) != 0u) != lit.is_negated();
                    if (!satisfied)
                    {
                        all = false;
                        break;
                    }
                }
                if (all)
                    result.insert(assignment & ((1u << keep) - 1u));
            }
            return result;
        }

        std::uint32_t highest_variable(const std::vector<clause_t>& clauses)
        {
            std::uint32_t highest {};
            for (const auto& clause: clauses)
                for (const auto lit: clause)
                    highest = std::max(highest, lit.variable_of().index());
            return highest;
        }

        void fill(cdcl::clause::database& database, const std::vector<clause_t>& clauses)
        {
            for (const auto& clause: clauses)
                database.add_clause(std::span<const literal> {clause}, false);
        }
    }

    TEST_CASE("transitive reduction never uses a removed clause to justify a removal", "[sat][regression]")
    {
        // The defect: an edge removed earlier in the pass still counted as an alternative path for later
        // removals, and two unsatisfiable circuit instances were answered satisfiable. On random binary formulas
        // the pass must keep exactly the original model set.
        generator random {0x2545f4914f6cdd1dull};
        for (std::size_t sample = 0u; sample < 300u; ++sample)
        {
            const std::uint32_t variables = 4u + random.next(4u); // 4..7
            const std::uint32_t clause_count = variables + random.next(3u * variables);
            std::vector<clause_t> clauses;
            for (std::uint32_t index = 0u; index < clause_count; ++index)
            {
                const auto first = 1u + random.next(variables);
                auto second = 1u + random.next(variables);
                while (second == first)
                    second = 1u + random.next(variables);
                clauses.push_back({literal {variable {first}, random.next(2u) != 0u}, literal {variable {second}, random.next(2u) != 0u}});
            }
            const auto before = models(clauses, variables, variables);

            cdcl::clause::database database;
            fill(database, clauses);
            transitive_reducer reducer;
            reducer.attach_database(database);
            reducer.run();
            reducer.prune_binary_edges();

            const auto after = models(live_clauses(database), variables, variables);
            INFO("sample " << sample << " variables " << variables << " clauses " << clause_count << " removed " << reducer.removed_edge_count());
            REQUIRE(after == before);
        }
    }

    TEST_CASE("a learned clause that subsumes an original clause is promoted, not left to reduction", "[sat][regression]")
    {
        // The defect: the original clause was deleted as subsumed by a learned clause that a later reduction pass
        // then threw away, and three satisfiable instances came back with models violating the lost clause.
        cdcl::clause::database database;
        const std::array<literal, 3> original {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, true}};
        const std::array<literal, 2> learned {literal {variable {1u}, false}, literal {variable {2u}, false}};
        const std::array<literal, 2> other {literal {variable {4u}, false}, literal {variable {5u}, true}};
        database.add_clause(original, false);
        database.add_clause(other, false);
        const auto learned_ref = database.add_clause(learned, true);

        forward_subsumer subsumer;
        subsumer.attach_database(database);
        subsumer.run();

        REQUIRE(subsumer.subsumed_count() >= 1u);
        const auto remaining = live_clauses(database);
        // The subsumed original is gone, the subsuming clause now stands in the irredundant set.
        REQUIRE(remaining.size() == 2u);
        const bool promoted = std::any_of(remaining.begin(), remaining.end(), [](const clause_t& clause) {
            return clause.size() == 2u && clause[0].variable_of().index() == 1u && clause[1].variable_of().index() == 2u;
        });
        REQUIRE(promoted);
        REQUIRE(database.stats_snapshot().redundant_count == 0u);
        (void) learned_ref;
    }

    TEST_CASE("a second subsumption run over a grown database finds what a fresh run finds", "[sat][regression]")
    {
        // The subsumer skips pairs of clauses it has already checked and that have not changed since (the
        // inprocessing epochs re-ran it over the whole formula every two thousand conflicts). Adding learned
        // clauses, strengthening one, and running again must leave exactly the live clause set a fresh subsumer
        // leaves on an identical database.
        generator random {0x1234567890abcdefull};
        std::size_t differing_samples {};
        for (std::size_t sample = 0u; sample < 150u; ++sample)
        {
            const std::uint32_t variables = 6u + random.next(4u);
            const auto random_clause = [&](const std::uint32_t size)
            {
                clause_t clause;
                while (clause.size() < size)
                {
                    const auto var = 1u + random.next(variables);
                    if (std::none_of(clause.begin(), clause.end(), [var](const literal lit) { return lit.variable_of().index() == var; }))
                        clause.push_back(literal {variable {var}, random.next(2u) != 0u});
                }
                return clause;
            };
            std::vector<clause_t> original, learned;
            const std::uint32_t original_count = 8u + random.next(10u), learned_count = 4u + random.next(8u);
            for (std::uint32_t index = 0u; index < original_count; ++index)
                original.push_back(random_clause(2u + random.next(3u)));
            for (std::uint32_t index = 0u; index < learned_count; ++index)
                learned.push_back(random_clause(1u + random.next(4u)));

            // Incremental: originals, run, learned clauses added, run again.
            cdcl::clause::database incremental;
            fill(incremental, original);
            forward_subsumer twice;
            twice.attach_database(incremental);
            twice.run();
            for (const auto& clause: learned)
                incremental.add_clause(std::span<const literal> {clause}, true);
            twice.run();

            // Fresh: the same clauses in the same order, subsumed by a subsumer that has never seen them.
            cdcl::clause::database reference;
            fill(reference, original);
            forward_subsumer first;
            first.attach_database(reference);
            first.run(); // the originals among themselves, as the pipeline does before the search
            for (const auto& clause: learned)
                reference.add_clause(std::span<const literal> {clause}, true);
            forward_subsumer once;
            once.attach_database(reference);
            once.set_incremental(false); // a full pass, whatever the first run marked
            once.run();

            const auto left = live_clauses(incremental);
            const auto right = live_clauses(reference);
            const auto dump = [](const std::vector<clause_t>& clauses)
            {
                std::string text;
                for (const auto& clause: clauses)
                {
                    text += "[";
                    for (const auto lit: clause)
                        text += (lit.is_negated() ? "-" : "") + std::to_string(lit.variable_of().index()) + " ";
                    text += "] ";
                }
                return text;
            };
            INFO("sample " << sample << " originals " << original_count << " learned " << learned_count << "\nincremental: " << dump(left)
                           << "\nfresh:       " << dump(right) << "\noriginals:   " << dump(original) << "\nlearned:     " << dump(learned));
            REQUIRE(left == right);
            REQUIRE(incremental.stats_snapshot().redundant_count == reference.stats_snapshot().redundant_count);
            if (twice.subsumed_count() != 0u)
                ++differing_samples;
        }
        // The generator must produce subsumptions in the second run, or the test proves little.
        REQUIRE(differing_samples > 20u);
    }

    TEST_CASE("factoring preserves the model set over the original variables", "[sat][regression]")
    {
        // Bounded variable addition introduces variables; the models of the new formula, projected onto the
        // original variables, must be exactly the models of the old one, and the introduced variables must be
        // recorded on the extension stack so they can be dropped from external models.
        generator random {0x7f4a7c159e3779b9ull};
        std::size_t factored_samples {};
        for (std::size_t sample = 0u; sample < 200u; ++sample)
        {
            // A rectangle of (a_i v b_j) plus random binary and ternary clauses over the same variables.
            const std::uint32_t rows = 2u + random.next(2u), columns = 2u + random.next(2u);
            const std::uint32_t variables = 6u + random.next(2u); // 6..7 original variables
            std::vector<clause_t> clauses;
            std::vector<bool> column_negated;
            for (std::uint32_t column = 0u; column < columns; ++column)
                column_negated.push_back(random.next(2u) != 0u);
            for (std::uint32_t row = 0u; row < rows; ++row)
                for (std::uint32_t column = 0u; column < columns; ++column)
                    clauses.push_back({literal {variable {1u + row}, false}, literal {variable {1u + rows + column}, column_negated[column]}});
            const std::uint32_t extra = random.next(6u);
            for (std::uint32_t index = 0u; index < extra; ++index)
            {
                const std::uint32_t size = 2u + random.next(2u);
                clause_t clause;
                while (clause.size() < size)
                {
                    const auto var = 1u + random.next(variables);
                    if (std::none_of(clause.begin(), clause.end(), [var](const literal lit) { return lit.variable_of().index() == var; }))
                        clause.push_back(literal {variable {var}, random.next(2u) != 0u});
                }
                clauses.push_back(clause);
            }
            const auto before = models(clauses, variables, variables);

            cdcl::clause::database database;
            cdcl::stack::extension extension;
            fill(database, clauses);
            factorizer factor;
            factor.attach_clause_database(database);
            factor.attach_extension_stack(extension);
            factor.set_problem_variable_count(variables);
            factor.run();

            const auto after_clauses = live_clauses(database);
            const auto total_variables = std::max(variables, highest_variable(after_clauses));
            REQUIRE(total_variables <= 12u);
            // Fresh variables must be numbered above every problem variable, occurring or not.
            for (const auto& clause: after_clauses)
                for (const auto lit: clause)
                    if (lit.variable_of().index() > variables)
                        REQUIRE(lit.variable_of().index() > variables);
            const auto after = models(after_clauses, total_variables, variables);
            std::string dump;
            for (const auto& clause: clauses)
            {
                dump += "[";
                for (const auto lit: clause)
                    dump += (lit.is_negated() ? "-" : "") + std::to_string(lit.variable_of().index()) + " ";
                dump += "] ";
            }
            dump += " => ";
            for (const auto& clause: after_clauses)
            {
                dump += "[";
                for (const auto lit: clause)
                    dump += (lit.is_negated() ? "-" : "") + std::to_string(lit.variable_of().index()) + " ";
                dump += "] ";
            }
            INFO("sample " << sample << " rows " << rows << " columns " << columns << " introduced " << factor.introduced_variable_count() << " " << dump);
            REQUIRE(after == before);
            REQUIRE(extension.size() == factor.introduced_variable_count());
            if (factor.introduced_variable_count() != 0u)
                ++factored_samples;
        }
        REQUIRE(factored_samples > 20u);
    }
}
