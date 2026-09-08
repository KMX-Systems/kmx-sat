/// @file library/src/kmx/sat/simplify/transitive_reducer.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/transitive_reducer.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/transitive_reducer.hpp>

namespace kmx::sat::simplify
{
    void transitive_reducer::prune_binary_edges() noexcept
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

    void transitive_reducer::build_binary_implication_graph() noexcept
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

            binary_clauses_.push_back(binary_clause {ref, literals[0u], literals[1u]});
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
        adjacency_fill_.assign(adjacency_start_.begin(), adjacency_start_.end() - 1L);
        for (std::uint32_t clause_index {}; clause_index < binary_clauses_.size(); ++clause_index)
        {
            const auto& clause = binary_clauses_[clause_index];
            adjacency_entries_[adjacency_fill_[clause.first.negated().raw()]++] = edge {clause.second.raw(), clause_index};
            adjacency_entries_[adjacency_fill_[clause.second.negated().raw()]++] = edge {clause.first.raw(), clause_index};
        }
        removed_.assign(binary_clauses_.size(), 0u);
    }

    [[nodiscard]] std::span<const transitive_reducer::edge> transitive_reducer::successors_of(const literal::raw_t from) const noexcept
    {
        const auto slot = static_cast<std::size_t>(from);
        if (slot + 1u >= adjacency_start_.size())
            return {};
        const auto begin = adjacency_start_[slot];
        return {adjacency_entries_.data() + begin, adjacency_start_[slot + 1u] - begin};
    }

    [[nodiscard]] bool transitive_reducer::has_alternative_path(const literal::raw_t source, const literal::raw_t target,
                                                                const std::uint32_t excluded_clause) noexcept
    {
        if (visit_stamp_.empty() || (effort_remaining_ == 0u))
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
                if ((clause_index == excluded_clause) || (removed_[clause_index] != 0u))
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
}
