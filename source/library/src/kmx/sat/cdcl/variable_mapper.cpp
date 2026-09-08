/// @file library/src/kmx/sat/cdcl/variable_mapper.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/variable_mapper.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/variable_mapper.hpp>

namespace kmx::sat::cdcl
{
    variable variable_mapper::ensure_external_variable(const variable var) noexcept
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

    literal variable_mapper::to_internal_literal(const literal lit) noexcept
    {
        const auto it = external_to_internal_.find(lit.variable_of().index());
        if (it == external_to_internal_.end())
            return lit;
        return literal {it->second, lit.is_negated()};
    }

    literal variable_mapper::to_external_literal(const literal lit) noexcept
    {
        const auto it = internal_to_external_.find(lit.variable_of().index());
        if (it == internal_to_external_.end())
            return lit;
        return literal {it->second, lit.is_negated()};
    }

    void variable_mapper::rebuild_after_compaction() noexcept
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

    void variable_mapper::rebuild_after_compaction(const std::unordered_map<std::uint32_t, variable>& permutation) noexcept
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

    variable_mapping_list_t variable_mapper::mapping_entries() const noexcept
    {
        variable_mapping_list_t entries {};
        entries.reserve(external_to_internal_.size());
        for (const auto& [external_index, internal_var]: external_to_internal_)
            entries.emplace_back(variable {external_index}, internal_var);
        return entries;
    }
}
