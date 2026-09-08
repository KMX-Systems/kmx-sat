/// @file library/src/kmx/sat/cdcl/store/assignment.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/store/assignment.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/store/assignment.hpp>

namespace kmx::sat::cdcl::store
{
    std::optional<bool> assignment::value_of(const variable var) const noexcept
    {
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            const auto it = sparse_values_.find(var.index());
            return (it == sparse_values_.end()) ? std::nullopt : it->second;
        }
        if (index >= values_.size())
            return {};
        return values_[index].has_value() ? std::optional<bool> {values_[index].value()} : std::nullopt;
    }

    void assignment::assign(const literal lit, const clause::ref_t reason) noexcept
    {
        const auto var = lit.variable_of();
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            const auto var_index = var.index();
            sparse_values_[var_index] = !lit.is_negated();
            sparse_reasons_[var_index] = reason;
            sparse_levels_[var_index] = current_level_;
            sparse_trail_positions_[var_index] = current_trail_position_;
            sparse_analyzed_[var_index] = false;
            return;
        }
        ensure_capacity(index);
        values_[index] = lit.is_negated() ? false : true;
        reasons_[index] = reason;
        levels_[index] = current_level_;
        trail_positions_[index] = current_trail_position_;
        analyzed_[index] = false;
    }

    void assignment::unassign_from(const variable var) noexcept
    {
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            const auto var_index = var.index();
            sparse_values_.erase(var_index);
            sparse_reasons_.erase(var_index);
            sparse_levels_.erase(var_index);
            sparse_trail_positions_.erase(var_index);
            sparse_analyzed_.erase(var_index);
            return;
        }
        if (index >= values_.size())
            return;
        values_[index].reset();
        reasons_[index] = {};
        levels_[index] = 0u;
        trail_positions_[index] = 0u;
        analyzed_[index] = false;
    }

    void assignment::unassign_above_level(const std::uint32_t level) noexcept
    {
        for (std::size_t index = 0u; index < values_.size(); ++index)
            if (values_[index].has_value() && (levels_[index] > level))
                unassign_from(variable {static_cast<std::uint32_t>(index)});
    }

    clause::ref_t assignment::reason_of(const variable var) const noexcept
    {
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            const auto it = sparse_reasons_.find(var.index());
            return (it == sparse_reasons_.end()) ? clause::ref_t {} : it->second;
        }
        if (index >= reasons_.size())
            return {};
        return reasons_[index];
    }

    std::uint32_t assignment::level_of(const variable var) const noexcept
    {
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            const auto it = sparse_levels_.find(var.index());
            return (it == sparse_levels_.end()) ? 0u : it->second;
        }
        if (index >= levels_.size())
            return 0u;
        return levels_[index];
    }

    std::uint32_t assignment::trail_position_of(const variable var) const noexcept
    {
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            const auto it = sparse_trail_positions_.find(var.index());
            return (it == sparse_trail_positions_.end()) ? 0u : it->second;
        }
        if (index >= trail_positions_.size())
            return 0u;
        return trail_positions_[index];
    }

    void assignment::mark_analyzed(const variable var) noexcept
    {
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            sparse_analyzed_[var.index()] = true;
            return;
        }
        if (index < analyzed_.size())
            analyzed_[index] = true;
    }

    bool assignment::analysis_seen(const variable var) const noexcept
    {
        const auto index = index_of(var);
        if (is_sparse(index))
        {
            const auto it = sparse_analyzed_.find(var.index());
            return (it != sparse_analyzed_.end()) && it->second;
        }
        if (index >= analyzed_.size())
            return false;
        return analyzed_[index];
    }

    void assignment::clear_analysis_marks() noexcept
    {
        for (std::size_t i = 0u; i < analyzed_.size(); ++i)
            analyzed_[i] = false;
        for (auto& entry: sparse_analyzed_)
            entry.second = false;
    }

    void assignment::rewrite_reasons_after_compaction(
        const std::unordered_map<clause::ref_t::offset_t, clause::ref_t::offset_t>& relocation) noexcept
    {
        if (relocation.empty())
            return;

        for (auto& reason: reasons_)
        {
            if (!reason.valid())
                continue;
            if (const auto it = relocation.find(reason.offset()); it != relocation.end())
                reason = clause::ref_t {it->second};
        }
        for (auto& [index, reason]: sparse_reasons_)
        {
            (void)index;
            if (const auto it = relocation.find(reason.offset()); it != relocation.end())
                reason = clause::ref_t {it->second};
        }
    }

    void assignment::ensure_capacity(const std::size_t index) noexcept
    {
        if (index >= values_.size())
        {
            const auto new_size = index + 1u;
            values_.resize(new_size);
            reasons_.resize(new_size);
            levels_.resize(new_size);
            trail_positions_.resize(new_size);
            analyzed_.resize(new_size);
        }
    }
}
