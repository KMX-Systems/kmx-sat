/// @file inc/kmx/sat/simplify/transitive_reducer.hpp
/// @brief Simplifies the binary implication graph before more expensive passes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify
{
    /// @brief Simplifies the binary implication graph before more expensive passes.
    /// @details
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

            build_binary_implication_graph();

            // Bounded: one breadth-first search per binary clause is quadratic in the worst case, and on a
            // 234k-clause colouring instance the unbounded pass was a third of the whole run while removing
            // nothing. The budget scales with the graph; when it runs out the remaining clauses are simply kept.
            effort_remaining_ = effort_per_edge * adjacency_entries_.size() + minimum_effort;

            for (std::uint32_t clause_index {}; clause_index < binary_clauses_.size(); ++clause_index)
            {
                const auto& clause = binary_clauses_[clause_index];
                if (!clause.ref.valid() || database_->is_garbage(clause.ref))
                    continue;
                if (effort_remaining_ == 0u)
                    break;

                // An implication `a -> b` is redundant when the other binary clauses already imply it. Only
                // clauses still present may serve as justification: an edge whose clause was removed earlier
                // in this pass is no longer there to imply anything, and letting two clauses justify each
                // other's removal weakens the formula (an unsatisfiable circuit instance became satisfiable).
                const auto first_source = clause.first.negated().raw();
                const auto first_target = clause.second.raw();
                const auto second_source = clause.second.negated().raw();
                const auto second_target = clause.first.raw();
                if (has_alternative_path(first_source, first_target, clause_index) &&
                    has_alternative_path(second_source, second_target, clause_index))
                {
                    if (proof_manager_ != nullptr)
                        proof_manager_->on_delete_clause(clause.ref);
                    database_->mark_garbage(clause.ref);
                    removed_[clause_index] = 1u;
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

        /// Compressed literal-to-successor adjacency, rebuilt into reusable buffers on each prune.
        void build_binary_implication_graph() noexcept
        {
            binary_clauses_.clear();
            literal::raw_t highest_raw {};
            const auto collect_clause = [&](const cdcl::clause::ref_t ref) noexcept
            {
                if (!ref.valid() || database_->is_garbage(ref))
                    return;

                const auto literals = database_->storage_of().view_literals(ref);
                if (literals.size() != 2u)
                    return;

                binary_clauses_.push_back(binary_clause {ref, literals[0], literals[1]});
                for (const auto lit: literals)
                {
                    if (lit.raw() > highest_raw)
                        highest_raw = lit.raw();
                    if (lit.negated().raw() > highest_raw)
                        highest_raw = lit.negated().raw();
                }
            };

            database_->iterate_irredundant(collect_clause);
            database_->iterate_redundant(collect_clause);

            const auto slots = binary_clauses_.empty() ? 0u : static_cast<std::size_t>(highest_raw) + 1u;
            adjacency_start_.assign(slots + 1u, 0u);
            adjacency_entries_.clear();
            visit_stamp_.assign(slots, 0u);
            visit_generation_ = 0u;
            if (slots == 0u)
                return;

            for (const auto& clause: binary_clauses_)
            {
                ++adjacency_start_[static_cast<std::size_t>(clause.first.negated().raw()) + 1u];
                ++adjacency_start_[static_cast<std::size_t>(clause.second.negated().raw()) + 1u];
            }
            for (std::size_t slot {1u}; slot <= slots; ++slot)
                adjacency_start_[slot] += adjacency_start_[slot - 1u];

            adjacency_entries_.resize(binary_clauses_.size() * 2u);
            adjacency_fill_.assign(adjacency_start_.begin(), adjacency_start_.end() - 1);
            for (std::uint32_t clause_index {}; clause_index < binary_clauses_.size(); ++clause_index)
            {
                const auto& clause = binary_clauses_[clause_index];
                adjacency_entries_[adjacency_fill_[clause.first.negated().raw()]++] = edge {clause.second.raw(), clause_index};
                adjacency_entries_[adjacency_fill_[clause.second.negated().raw()]++] = edge {clause.first.raw(), clause_index};
            }
            removed_.assign(binary_clauses_.size(), 0u);
        }

        /// @brief One implication edge of the binary graph, tagged with the clause it comes from.
        struct edge final
        {
            literal::raw_t target {};
            std::uint32_t clause_index {};
        };

        [[nodiscard]] std::span<const edge> successors_of(const literal::raw_t from) const noexcept
        {
            const auto slot = static_cast<std::size_t>(from);
            if (slot + 1u >= adjacency_start_.size())
                return {};
            const auto begin = adjacency_start_[slot];
            return {adjacency_entries_.data() + begin, adjacency_start_[slot + 1u] - begin};
        }

        /// @brief Breadth-first search for `source -> ... -> target` over binary clauses other than
        /// `excluded_clause` and other than any clause removed earlier in this pass.
        [[nodiscard]] bool has_alternative_path(const literal::raw_t source, const literal::raw_t target,
                                                const std::uint32_t excluded_clause) noexcept
        {
            if (visit_stamp_.empty() || effort_remaining_ == 0u)
                return false;

            if (++visit_generation_ == 0u)
            {
                visit_stamp_.assign(visit_stamp_.size(), 0u);
                visit_generation_ = 1u;
            }

            frontier_.clear();
            frontier_.push_back(source);
            visit_stamp_[source] = visit_generation_;

            for (std::size_t index {}; index < frontier_.size(); ++index)
            {
                const auto current = frontier_[index];
                const auto successors = successors_of(current);
                if (effort_remaining_ <= successors.size())
                {
                    effort_remaining_ = 0u;
                    return false;
                }
                effort_remaining_ -= successors.size();
                for (const auto [next, clause_index]: successors)
                {
                    if (clause_index == excluded_clause || removed_[clause_index] != 0u)
                        continue;

                    if (next == target)
                        return true;

                    if (visit_stamp_[next] != visit_generation_)
                    {
                        visit_stamp_[next] = visit_generation_;
                        frontier_.push_back(next);
                    }
                }
            }

            return false;
        }

        /// @brief Search steps allowed per implication edge of the graph.
        static constexpr std::size_t effort_per_edge {2u};
        /// @brief Search steps every pass gets regardless of graph size, so small formulas are reduced completely.
        static constexpr std::size_t minimum_effort {1u << 18u};

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::vector<binary_clause> binary_clauses_ {};
        std::vector<std::uint32_t> adjacency_start_ {};
        std::vector<edge> adjacency_entries_ {};
        std::vector<std::uint32_t> adjacency_fill_ {};
        std::vector<std::uint8_t> removed_ {};
        std::size_t effort_remaining_ {};
        std::vector<std::uint32_t> visit_stamp_ {};
        std::vector<literal::raw_t> frontier_ {};
        std::uint32_t visit_generation_ {};
        std::size_t removed_edge_count_ {};
        bool pruned_ {};
    };
}
