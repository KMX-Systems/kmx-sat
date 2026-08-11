/// @file inc/kmx/sat/cdcl/store/assignment.hpp
/// @brief SoA storage for values, levels, reasons, trail positions, and auxiliary flags.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <optional>
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
            if (index >= values_.size())
            {
                return std::nullopt;
            }
            return values_[index].has_value() ? std::optional<bool> {values_[index].value()} : std::nullopt;
        }

        void assign(const literal lit, const clause::ref_t reason) noexcept
        {
            const auto var = lit.variable_of();
            const auto index = index_of(var);
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
            if (index >= values_.size())
            {
                return;
            }
            values_[index].reset();
            reasons_[index] = {};
            levels_[index] = 0;
            trail_positions_[index] = 0;
            analyzed_[index] = false;
        }

        void unassign_above_level(const std::uint32_t level) noexcept
        {
            for (std::size_t index = 0; index < values_.size(); ++index)
            {
                if (values_[index].has_value() && levels_[index] > level)
                {
                    unassign_from(variable {static_cast<std::uint32_t>(index)});
                }
            }
        }

        clause::ref_t reason_of(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (index >= reasons_.size())
            {
                return {};
            }
            return reasons_[index];
        }

        std::uint32_t level_of(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (index >= levels_.size())
            {
                return 0;
            }
            return levels_[index];
        }

        std::uint32_t trail_position_of(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (index >= trail_positions_.size())
            {
                return 0;
            }
            return trail_positions_[index];
        }

        void mark_analyzed(const variable var) noexcept
        {
            const auto index = index_of(var);
            if (index < analyzed_.size())
            {
                analyzed_[index] = true;
            }
        }

        void mark_analysis_seen(const variable var) noexcept
        {
            mark_analyzed(var);
        }

        bool analysis_seen(const variable var) const noexcept
        {
            const auto index = index_of(var);
            if (index >= analyzed_.size())
            {
                return false;
            }
            return analyzed_[index];
        }

        void clear_analysis_marks() noexcept
        {
            for (std::size_t i = 0; i < analyzed_.size(); ++i)
            {
                analyzed_[i] = false;
            }
        }

        void set_current_level(const std::uint32_t level) noexcept
        {
            current_level_ = level;
        }

        void set_current_trail_position(const std::uint32_t position) noexcept
        {
            current_trail_position_ = position;
        }

    private:
        static std::size_t index_of(const variable var) noexcept
        {
            return static_cast<std::size_t>(var.index());
        }

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
        std::uint32_t current_level_ {0};
        std::uint32_t current_trail_position_ {0};
    };
}
