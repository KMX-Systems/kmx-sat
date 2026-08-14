/// @file inc/kmx/sat/simplify/transitive_reducer.hpp
/// @brief Simplifies the binary implication graph before more expensive passes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify
{
    /// @brief Simplifies the binary implication graph before more expensive passes.
    ///
    /// The binary implication graph (one node per literal, one edge per binary clause) often contains redundant
    /// edges implied transitively by other edges; `transitive_reducer` removes such edges so later passes that
    /// traverse this graph (`engine::decomposition`'s SCC computation, `engine::probing`'s hyper-binary learning,
    /// `engine::sweep`'s micro-instance construction) operate on a smaller, denser-information graph.
    /// `prune_binary_edges` performs the actual reduction; `report_removed_edges` exposes the count for telemetry;
    /// `run` drives one full pass. Being purely a graph simplification over binary clauses, any edge removal here
    /// must still be reported to `proof::proof_manager` as an ordinary clause deletion.
    class transitive_reducer final
    {
    public:
        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        transitive_reducer() noexcept = default;

        /// @brief Attaches the clause database whose binary clauses form the implication graph.
        /// @param database Clause database to scan and mutate.
        /// @throws None (noexcept).
        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Attaches the proof manager used to report clause deletions.
        /// @param proof_manager Proof manager to notify.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void run() noexcept
        {
            removed_edge_count_ = 0u;
            pruned_ = false;
        }

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void prune_binary_edges() noexcept
        {
            if (database_ == nullptr)
            {
                pruned_ = true;
                return;
            }

            std::vector<binary_clause> binary_clauses {};
            auto adjacency = collect_binary_implication_graph(binary_clauses);

            for (const auto& clause: binary_clauses)
            {
                if (!clause.ref.valid() || database_->is_garbage(clause.ref))
                    continue;

                const auto first_source = clause.first.negated().raw();
                const auto first_target = clause.second.raw();
                const auto second_source = clause.second.negated().raw();
                const auto second_target = clause.first.raw();
                if (has_alternative_path(adjacency, first_source, first_target, clause) &&
                    has_alternative_path(adjacency, second_source, second_target, clause))
                {
                    if (proof_manager_ != nullptr)
                        proof_manager_->on_delete_clause(clause.ref);
                    database_->mark_garbage(clause.ref);
                    ++removed_edge_count_;
                }
            }

            database_->flush_satisfied([&](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
            pruned_ = true;
        }

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void report_removed_edges() const noexcept {}

        std::size_t removed_edge_count() const noexcept { return removed_edge_count_; }

        bool pruned() const noexcept { return pruned_; }

    private:
        struct binary_clause final
        {
            cdcl::clause::ref_t ref {};
            literal first {};
            literal second {};
        };

        using adjacency_map = std::unordered_map<literal::raw_t, std::vector<literal::raw_t>>;

        [[nodiscard]] adjacency_map collect_binary_implication_graph(std::vector<binary_clause>& binary_clauses) const noexcept
        {
            adjacency_map adjacency {};
            const auto collect_clause = [&](const cdcl::clause::ref_t ref) noexcept
            {
                if (!ref.valid() || database_->is_garbage(ref))
                    return;

                const auto literals = database_->storage_of().literals_of(ref);
                if (literals.size() != 2u)
                    return;

                binary_clauses.push_back(binary_clause {ref, literals[0], literals[1]});
                adjacency[literals[0].negated().raw()].push_back(literals[1].raw());
                adjacency[literals[1].negated().raw()].push_back(literals[0].raw());
            };

            database_->iterate_irredundant(collect_clause);
            database_->iterate_redundant(collect_clause);
            return adjacency;
        }

        [[nodiscard]] static bool is_excluded_edge(const literal::raw_t from, const literal::raw_t to, const binary_clause& clause) noexcept
        {
            return (from == clause.first.negated().raw() && to == clause.second.raw()) ||
                   (from == clause.second.negated().raw() && to == clause.first.raw());
        }

        [[nodiscard]] static bool has_alternative_path(const adjacency_map& adjacency, const literal::raw_t source,
                                                       const literal::raw_t target, const binary_clause& excluded_clause) noexcept
        {
            std::vector<literal::raw_t> frontier {source};
            std::unordered_map<literal::raw_t, bool> visited {};
            visited[source] = true;

            for (std::size_t index {}; index < frontier.size(); ++index)
            {
                const auto current = frontier[index];
                const auto it = adjacency.find(current);
                if (it == adjacency.end())
                    continue;

                for (const auto next: it->second)
                {
                    if (is_excluded_edge(current, next, excluded_clause))
                        continue;

                    if (next == target && current != source)
                        return true;

                    if (!visited.contains(next))
                    {
                        visited[next] = true;
                        frontier.push_back(next);
                    }
                }
            }

            return false;
        }

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::size_t removed_edge_count_ {};
        bool pruned_ {};
    };
}
