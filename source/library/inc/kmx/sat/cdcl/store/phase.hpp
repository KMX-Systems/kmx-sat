/// @file inc/kmx/sat/cdcl/store/phase.hpp
/// @brief All phase saving and rephasing policies.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief All phase saving and rephasing policies.
    /// @details
    /// `phase` stores polarity preferences per variable (saved, best, target) used by decision heuristics.
    /// It supports deterministic transformations such as full phase flipping and subset randomization strategies.
    class phase final
    {
    public:
        phase() noexcept = default;

        bool saved_phase(const variable var) const noexcept
        {
            return value_at(var, saved_phases_);
        }

        bool best_phase(const variable var) const noexcept
        {
            return value_at(var, best_phases_);
        }

        bool target_phase(const variable var) const noexcept
        {
            return value_at(var, target_phases_);
        }

        void set_saved_phase(const variable var, const bool value) noexcept
        {
            set_value(var, value, saved_phases_);
        }

        void flip_all() noexcept
        {
            for (std::size_t i = 0; i < saved_phases_.size(); ++i)
            {
                saved_phases_[i] = saved_phases_[i] == 0u ? 1u : 0u;
            }
        }

        void randomize_subset() noexcept
        {
            for (std::size_t i = 0; i < saved_phases_.size(); ++i)
            {
                saved_phases_[i] = (saved_phases_[i] + 1u) & 1u;
            }
        }

    private:
        static std::size_t index_of(const variable var) noexcept
        {
            return static_cast<std::size_t>(var.index());
        }

        static bool value_at(const variable var, const std::vector<std::uint8_t>& storage) noexcept
        {
            const auto index = index_of(var);
            if (index >= storage.size())
            {
                return false;
            }
            return storage[index] != 0u;
        }

        static void set_value(const variable var, const bool value, std::vector<std::uint8_t>& storage) noexcept
        {
            const auto index = index_of(var);
            if (index >= storage.size())
            {
                storage.resize(index + 1, 0u);
            }
            storage[index] = value ? 1u : 0u;
        }

        std::vector<std::uint8_t> saved_phases_ {};
        std::vector<std::uint8_t> best_phases_ {};
        std::vector<std::uint8_t> target_phases_ {};
    };
}
