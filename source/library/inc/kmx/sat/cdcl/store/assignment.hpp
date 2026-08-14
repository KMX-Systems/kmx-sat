/// @file inc/kmx/sat/cdcl/store/assignment.hpp
/// @brief SoA storage for values, levels, reasons, trail positions, and auxiliary flags.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief SoA storage for values, levels, reasons, trail positions, and auxiliary flags.
    /// @details
    /// `assignment` keeps variable state in parallel arrays to preserve compact, cache-friendly access patterns.
    /// It records the current truth value, implication reason, decision level, and trail position for each variable,
    /// and exposes transient analysis marks used by conflict analysis.
    class assignment final
    {
    public:
        assignment() noexcept = default;

        std::optional<bool> value_of(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (is_sparse(index))
            {
                const auto it = sparse_values_.find(var.index());
                return it == sparse_values_.end() ? std::nullopt : it->second;
            }
            if (index >= values_.size())
                return {};
            return values_[index].has_value() ? std::optional<bool> {values_[index].value()} : std::nullopt;
        }

        void assign(const literal lit, const clause::ref_t reason) noexcept
        {
            const auto var = lit.variable_of();
            const auto index = index_of(var);
            if (is_sparse(index))
            {
                sparse_values_[var.index()] = !lit.is_negated();
                sparse_reasons_[var.index()] = reason;
                sparse_levels_[var.index()] = current_level_;
                sparse_trail_positions_[var.index()] = current_trail_position_;
                sparse_analyzed_[var.index()] = false;
                return;
            }
            ensure_capacity(index);
            values_[index] = lit.is_negated() ? false : true;
            reasons_[index] = reason;
            levels_[index] = current_level_;
            trail_positions_[index] = current_trail_position_;
            analyzed_[index] = false;
        }

        void unassign_from(const variable var) noexcept
        {
            const auto index = index_of(var);
            if (is_sparse(index))
            {
                sparse_values_.erase(var.index());
                sparse_reasons_.erase(var.index());
                sparse_levels_.erase(var.index());
                sparse_trail_positions_.erase(var.index());
                sparse_analyzed_.erase(var.index());
                return;
            }
            if (index >= values_.size())
                return;
            values_[index].reset();
            reasons_[index] = {};
            levels_[index] = 0;
            trail_positions_[index] = 0;
            analyzed_[index] = false;
        }

        void unassign_above_level(const std::uint32_t level) noexcept
        {
            for (std::size_t index = 0; index < values_.size(); ++index)
                if (values_[index].has_value() && levels_[index] > level)
                    unassign_from(variable {static_cast<std::uint32_t>(index)});
        }

        clause::ref_t reason_of(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (is_sparse(index))
            {
                const auto it = sparse_reasons_.find(var.index());
                return it == sparse_reasons_.end() ? clause::ref_t {} : it->second;
            }
            if (index >= reasons_.size())
                return {};
            return reasons_[index];
        }

        std::uint32_t level_of(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (is_sparse(index))
            {
                const auto it = sparse_levels_.find(var.index());
                return it == sparse_levels_.end() ? 0u : it->second;
            }
            if (index >= levels_.size())
                return 0;
            return levels_[index];
        }

        std::uint32_t trail_position_of(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (is_sparse(index))
            {
                const auto it = sparse_trail_positions_.find(var.index());
                return it == sparse_trail_positions_.end() ? 0u : it->second;
            }
            if (index >= trail_positions_.size())
                return 0;
            return trail_positions_[index];
        }

        void mark_analyzed(const variable var) noexcept
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

        void mark_analysis_seen(const variable var) noexcept { mark_analyzed(var); }

        bool analysis_seen(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (is_sparse(index))
            {
                const auto it = sparse_analyzed_.find(var.index());
                return it != sparse_analyzed_.end() && it->second;
            }
            if (index >= analyzed_.size())
                return false;
            return analyzed_[index];
        }

        void clear_analysis_marks() noexcept
        {
            for (std::size_t i = 0; i < analyzed_.size(); ++i)
                analyzed_[i] = false;
            for (auto& entry: sparse_analyzed_)
                entry.second = false;
        }

        void set_current_level(const std::uint32_t level) noexcept { current_level_ = level; }

        void set_current_trail_position(const std::uint32_t position) noexcept { current_trail_position_ = position; }

        /// @brief Rewrites every stored implication reason reference according to a relocation map.
        /// @param relocation Mapping from old clause offsets to relocated clause offsets.
        /// @throws None (noexcept).
        void rewrite_reasons_after_compaction(
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
                (void) index;
                if (const auto it = relocation.find(reason.offset()); it != relocation.end())
                    reason = clause::ref_t {it->second};
            }
        }

        /// @brief Visits every stored implication reason currently tracked by the assignment store.
        /// @param visitor Callable invoked with each stored reason reference.
        /// @throws None (noexcept).
        template <typename visitor_t>
        void iterate_reasons(visitor_t&& visitor) const noexcept
        {
            for (const auto reason: reasons_)
                if (reason.valid())
                    visitor(reason);
            for (const auto& [index, reason]: sparse_reasons_)
            {
                (void) index;
                if (reason.valid())
                    visitor(reason);
            }
        }

    private:
        static constexpr std::size_t sparse_variable_threshold = 1u << 20;

        static std::size_t index_of(const variable var) noexcept { return static_cast<std::size_t>(var.index()); }

        static bool is_sparse(const std::size_t index) noexcept { return index >= sparse_variable_threshold; }

        void ensure_capacity(const std::size_t index) noexcept
        {
            if (index >= values_.size())
            {
                const auto new_size = index + 1;
                values_.resize(new_size);
                reasons_.resize(new_size);
                levels_.resize(new_size);
                trail_positions_.resize(new_size);
                analyzed_.resize(new_size);
            }
        }

        std::vector<std::optional<bool>> values_ {};
        std::vector<clause::ref_t> reasons_ {};
        std::vector<std::uint32_t> levels_ {};
        std::vector<std::uint32_t> trail_positions_ {};
        std::vector<bool> analyzed_ {};
        std::unordered_map<std::uint32_t, std::optional<bool>> sparse_values_ {};
        std::unordered_map<std::uint32_t, clause::ref_t> sparse_reasons_ {};
        std::unordered_map<std::uint32_t, std::uint32_t> sparse_levels_ {};
        std::unordered_map<std::uint32_t, std::uint32_t> sparse_trail_positions_ {};
        std::unordered_map<std::uint32_t, bool> sparse_analyzed_ {};
        std::uint32_t current_level_ {};
        std::uint32_t current_trail_position_ {};
    };
}
