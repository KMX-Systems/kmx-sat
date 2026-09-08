/// @file library/src/kmx/sat/simplify/equivalence_substitutor.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/equivalence_substitutor.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/equivalence_substitutor.hpp>

namespace kmx::sat::simplify
{
    void equivalence_substitutor::apply_equivalence_class(const std::uint32_t from, const std::uint32_t to) noexcept
    {
        if ((from == 0u) || (to == 0u) || (from == to))
            return;

        pending_rewrites_.push_back({from, to});
        rewrite_map_[from] = to;
        ++equivalence_class_count_;
        rewrite_completed_ = false;
    }

    void equivalence_substitutor::rewrite_clauses() noexcept
    {
        if (!pending_rewrites_.empty())
        {
            if (clause_database_ != nullptr)
            {
                rewrite_clause_set(clause_database_->irredundant_refs());
                rewrite_clause_set(clause_database_->redundant_refs());
            }
            ++clause_rewrite_count_;
        }
    }

    void equivalence_substitutor::rewrite_watches() noexcept
    {
        if (!pending_rewrites_.empty())
        {
            if (watch_list_ != nullptr)
                watch_list_->reindex_after_compaction([&](const literal old_literal) noexcept { return remap_literal(old_literal); });
            ++watch_rewrite_count_;
        }
    }

    void equivalence_substitutor::rewrite_external_mapping() noexcept
    {
        if (!pending_rewrites_.empty())
        {
            if (variable_mapper_ != nullptr)
            {
                std::unordered_map<std::uint32_t, variable> permutation {};
                permutation.reserve(pending_rewrites_.size());
                for (const auto& [from, to]: pending_rewrites_)
                    permutation[from] = variable {resolve_representative(to)};
                variable_mapper_->rebuild_after_compaction(permutation);
            }
            ++external_mapping_rewrite_count_;
            last_applied_rewrite_count_ = pending_rewrites_.size();
            pending_rewrites_.clear();
            rewrite_completed_ = true;
        }
    }

    std::uint32_t equivalence_substitutor::resolve_representative(std::uint32_t value) const noexcept
    {
        std::size_t guard {};
        while (guard++ < rewrite_map_.size())
        {
            const auto it = rewrite_map_.find(value);
            if ((it == rewrite_map_.end()) || (it->second == value))
                break;
            value = it->second;
        }
        return value;
    }

    literal equivalence_substitutor::remap_literal(const literal old_literal) const noexcept
    {
        const auto old_var = old_literal.variable_of().index();
        const auto new_var = resolve_representative(old_var);
        if (new_var == old_var)
            return old_literal;
        return literal {variable {new_var}, old_literal.is_negated()};
    }

    void equivalence_substitutor::rewrite_clause_set(const std::span<const cdcl::clause::ref_t> refs) noexcept
    {
        auto& storage = clause_database_->storage_of();
        for (const auto ref: refs)
        {
            if (!ref.valid() || clause_database_->is_garbage(ref))
                continue;

            auto literals = storage.literals_of(ref);
            bool changed {};
            for (auto& lit: literals)
            {
                const auto remapped = remap_literal(lit);
                if (remapped != lit)
                {
                    lit = remapped;
                    changed = true;
                }
            }

            if (changed)
                storage.rewrite_clause_literals(ref, literals);
        }
    }
}
