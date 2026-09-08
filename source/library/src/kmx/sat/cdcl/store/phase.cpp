/// @file library/src/kmx/sat/cdcl/store/phase.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/store/phase.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/store/phase.hpp>

namespace kmx::sat::cdcl::store
{
    void phase::seed_saved_phases(const std::span<const std::uint8_t> assignment) noexcept
    {
        if (assignment.size() <= 1u)
            return;
        if (saved_phases_.size() < assignment.size())
            saved_phases_.resize(assignment.size(), 0u);
        for (std::size_t index = 1u; index < assignment.size(); ++index)
            saved_phases_[index] = (assignment[index] != 0u) ? 1u : 0u;
    }

    bool phase::value_at(const variable var, const std::vector<std::uint8_t>& storage) noexcept
    {
        const auto index = index_of(var);
        if (index >= storage.size())
            return false;
        return storage[index] != 0u;
    }

    void phase::set_value(const variable var, const bool value, std::vector<std::uint8_t>& storage) noexcept
    {
        const auto index = index_of(var);
        if (index >= storage.size())
            storage.resize(index + 1u, 0u);
        storage[index] = value ? 1u : 0u;
    }
}
