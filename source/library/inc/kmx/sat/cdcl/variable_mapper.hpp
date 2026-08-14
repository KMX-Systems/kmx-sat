/// @file inc/kmx/sat/cdcl/variable_mapper.hpp
/// @brief e2i/i2e mapping and semantic stability of external variables.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <unordered_map>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief e2i/i2e mapping and semantic stability of external variables.
    /// @details
    /// `variable_mapper` owns the external-to-internal (e2i) and internal-to-external (i2e) variable tables consumed
    /// by `external_frontend`. `ensure_external_variable` allocates or looks up the internal slot for a caller-facing
    /// variable, guaranteeing internal-only variables introduced by `factorizer`/`gate_extractor` are drawn from a
    /// disjoint reserved range so they can never alias an external id. `to_internal_literal`/`to_external_literal`
    /// perform the per-literal translation on the hot path. `rebuild_after_compaction` re-derives both tables after
    /// `compaction_service` computes a new variable permutation (following BVE/BCE-driven elimination), while
    /// `mark_inactive`/`mark_eliminated` record variables that must be excluded from future model exposure by
    /// `model_reconstructor::drop_internal_only_variables`.
    /// @note Internal variable numbering may be reset across `solver::reset_session`, but within one incremental
    /// session internal variable identity must be preserved across `compaction_service` runs through the same
    /// permutation machinery used for external variables.
    class variable_mapper final
    {
    public:
        /// @brief Constructs a variable mapper with empty e2i/i2e tables.
        /// @throws None (noexcept).
        variable_mapper() noexcept = default;

        /// @brief Ensures an external variable has a corresponding internal slot, allocating one on first use.
        /// @param var External variable supplied by the caller.
        /// @return Internal variable identifier bound to `var`.
        /// @throws None (noexcept).
        variable ensure_external_variable(const variable var) noexcept
        {
            const auto it = external_to_internal_.find(var.index());
            if (it != external_to_internal_.end())
                return it->second;

            const variable internal {next_internal_index_};
            ++next_internal_index_;
            external_to_internal_[var.index()] = internal;
            internal_to_external_[internal.index()] = var;
            return internal;
        }

        /// @brief Translates one external literal into its internal representation using the e2i table.
        /// @param lit External literal.
        /// @return Internal literal.
        /// @throws None (noexcept).
        literal to_internal_literal(const literal lit) noexcept
        {
            const auto it = external_to_internal_.find(lit.variable_of().index());
            if (it == external_to_internal_.end())
                return lit;
            return literal {it->second, lit.is_negated()};
        }

        /// @brief Translates one internal literal into its external representation using the i2e table.
        /// @param lit Internal literal.
        /// @return External literal.
        /// @throws None (noexcept).
        literal to_external_literal(const literal lit) noexcept
        {
            const auto it = internal_to_external_.find(lit.variable_of().index());
            if (it == internal_to_external_.end())
                return lit;
            return literal {it->second, lit.is_negated()};
        }

        /// @brief Recomputes the e2i/i2e tables after `compaction_service` applies a variable permutation.
        /// @throws None (noexcept).
        void rebuild_after_compaction() noexcept
        {
            if (external_to_internal_.empty())
                return;

            std::unordered_map<std::uint32_t, variable> rebuilt_external_to_internal = external_to_internal_;
            std::unordered_map<std::uint32_t, variable> rebuilt_internal_to_external;
            for (const auto& [external_index, internal_var]: rebuilt_external_to_internal)
                rebuilt_internal_to_external[internal_var.index()] = variable {external_index};

            external_to_internal_ = std::move(rebuilt_external_to_internal);
            internal_to_external_ = std::move(rebuilt_internal_to_external);
        }

        /// @brief Recomputes the e2i/i2e tables after applying an explicit internal-variable permutation.
        /// @param permutation Mapping from old internal variable indices to their new compacted identifiers.
        /// @throws None (noexcept).
        void rebuild_after_compaction(const std::unordered_map<std::uint32_t, variable>& permutation) noexcept
        {
            if (external_to_internal_.empty())
                return;

            std::unordered_map<std::uint32_t, variable> rebuilt_external_to_internal = external_to_internal_;
            std::unordered_map<std::uint32_t, variable> rebuilt_internal_to_external;
            std::uint32_t highest_internal_index {internal_variable_base_};

            for (auto& [external_index, internal_var]: rebuilt_external_to_internal)
            {
                if (const auto it = permutation.find(internal_var.index()); it != permutation.end())
                    internal_var = it->second;

                rebuilt_internal_to_external[internal_var.index()] = variable {external_index};
                highest_internal_index = std::max(highest_internal_index, internal_var.index());
            }

            external_to_internal_ = std::move(rebuilt_external_to_internal);
            internal_to_external_ = std::move(rebuilt_internal_to_external);
            next_internal_index_ = highest_internal_index + 1u;
        }

        /// @brief Returns the currently known external-to-internal mapping entries.
        /// @return Snapshot of external variables paired with their internal identifiers.
        std::vector<std::pair<variable, variable>> mapping_entries() const noexcept
        {
            std::vector<std::pair<variable, variable>> entries {};
            entries.reserve(external_to_internal_.size());
            for (const auto& [external_index, internal_var]: external_to_internal_)
                entries.emplace_back(variable {external_index}, internal_var);
            return entries;
        }

        /// @brief Marks a variable inactive (for example melted or otherwise no longer part of the live problem)
        /// without discarding its identity mapping.
        /// @param var Variable to mark inactive.
        /// @throws None (noexcept).
        void mark_inactive(const variable var) noexcept { inactive_variables_.insert(var.index()); }

        /// @brief Marks a variable as eliminated by a simplification pass so it is excluded from future model
        /// exposure until reconstructed by `model_reconstructor`.
        /// @param var Variable to mark eliminated.
        /// @throws None (noexcept).
        void mark_eliminated(const variable var) noexcept { eliminated_variables_.insert(var.index()); }

        /// @brief Clears the inactive flag for a variable after it is melted or otherwise re-enabled.
        /// @param var Variable to reactivate.
        /// @throws None (noexcept).
        void mark_active(const variable var) noexcept { inactive_variables_.erase(var.index()); }

        /// @brief Returns whether a variable has been marked eliminated.
        bool is_eliminated(const variable var) const noexcept { return eliminated_variables_.contains(var.index()); }

        /// @brief Returns whether a variable has been marked inactive.
        bool is_inactive(const variable var) const noexcept { return inactive_variables_.contains(var.index()); }

    private:
        static constexpr std::uint32_t internal_variable_base_ {1u << 30};

        std::unordered_map<std::uint32_t, variable> external_to_internal_ {};
        std::unordered_map<std::uint32_t, variable> internal_to_external_ {};
        std::unordered_set<std::uint32_t> inactive_variables_ {};
        std::unordered_set<std::uint32_t> eliminated_variables_ {};
        std::uint32_t next_internal_index_ {internal_variable_base_};
    };
}
