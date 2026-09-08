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
    /// @brief Dense per-variable truth values, absent where the variable is unassigned.
    using value_list_t = std::vector<std::optional<bool>>;

    /// @brief Sparse per-variable truth values for indices outside the dense range.
    using sparse_value_map_t = std::unordered_map<std::uint32_t, std::optional<bool>>;

    /// @brief SoA storage for values, levels, reasons, trail positions, and auxiliary flags.
    /// @details
    /// `assignment` keeps variable state in parallel arrays to preserve compact, cache-friendly access patterns.
    /// It records the current truth value, implication reason, decision level, and trail position for each variable,
    /// and exposes transient analysis marks used by conflict analysis.
    class assignment final
    {
    public:
        assignment() noexcept = default;

        std::optional<bool> value_of(const variable var) const noexcept;

        void assign(const literal lit, const clause::ref_t reason) noexcept;

        void unassign_from(const variable var) noexcept;

        void unassign_above_level(const std::uint32_t level) noexcept;

        clause::ref_t reason_of(const variable var) const noexcept;

        std::uint32_t level_of(const variable var) const noexcept;

        std::uint32_t trail_position_of(const variable var) const noexcept;

        void mark_analyzed(const variable var) noexcept;

        void mark_analysis_seen(const variable var) noexcept { mark_analyzed(var); }

        bool analysis_seen(const variable var) const noexcept;

        void clear_analysis_marks() noexcept;

        void set_current_level(const std::uint32_t level) noexcept { current_level_ = level; }

        void set_current_trail_position(const std::uint32_t position) noexcept { current_trail_position_ = position; }

        /// @brief Rewrites every stored implication reason reference according to a relocation map.
        /// @param relocation Mapping from old clause offsets to relocated clause offsets.
        /// @throws None (noexcept).
        void rewrite_reasons_after_compaction(
            const std::unordered_map<clause::ref_t::offset_t, clause::ref_t::offset_t>& relocation) noexcept;

        /// @brief Visits every stored implication reason currently tracked by the assignment store.
        /// @param visitor Callable invoked with each stored reason reference.
        /// @throws None (noexcept).
        template <typename Visitor>
        void iterate_reasons(Visitor&& visitor) const noexcept
        {
            for (const auto reason: reasons_)
                if (reason.valid())
                    visitor(reason);
            for (const auto& [index, reason]: sparse_reasons_)
            {
                (void)index;
                if (reason.valid())
                    visitor(reason);
            }
        }

    private:
        static constexpr std::size_t sparse_variable_threshold = 1u << 20;

        static std::size_t index_of(const variable var) noexcept { return static_cast<std::size_t>(var.index()); }

        static bool is_sparse(const std::size_t index) noexcept { return index >= sparse_variable_threshold; }

        void ensure_capacity(const std::size_t index) noexcept;

        value_list_t values_ {};
        std::vector<clause::ref_t> reasons_ {};
        std::vector<std::uint32_t> levels_ {};
        std::vector<std::uint32_t> trail_positions_ {};
        std::vector<bool> analyzed_ {};
        sparse_value_map_t sparse_values_ {};
        std::unordered_map<std::uint32_t, clause::ref_t> sparse_reasons_ {};
        std::unordered_map<std::uint32_t, std::uint32_t> sparse_levels_ {};
        std::unordered_map<std::uint32_t, std::uint32_t> sparse_trail_positions_ {};
        std::unordered_map<std::uint32_t, bool> sparse_analyzed_ {};
        std::uint32_t current_level_ {};
        std::uint32_t current_trail_position_ {};
    };
}
