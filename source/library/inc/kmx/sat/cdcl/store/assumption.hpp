/// @file inc/kmx/sat/cdcl/store/assumption.hpp
/// @brief Stores assumptions separately from the decision trail.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief Stores assumptions separately from the decision trail.
    /// @details
    /// `assumption` maintains per-episode assumptions independent of the permanent clause database.
    /// It also tracks a failed-assumption subset after UNSAT under assumptions so the facade can expose a failed
    /// core without mutating base problem clauses.
    class assumption final
    {
    public:
        assumption() noexcept = default;

        void push(const literal lit) noexcept { literals_.push_back(lit); }

        void clear() noexcept;

        std::span<const literal> iterate() const noexcept { return literals_; }

        std::size_t size() const noexcept { return literals_.size(); }

        std::uint32_t trail_level_base() const noexcept { return trail_level_base_; }

        void set_trail_level_base(const std::uint32_t base) noexcept { trail_level_base_ = base; }

        std::span<const literal> capture_failed_assumptions() const noexcept { return failed_assumptions_; }

        void record_failed_assumptions(std::span<const literal> failures) noexcept
        {
            failed_assumptions_.assign(failures.begin(), failures.end());
        }

    private:
        std::vector<literal> literals_ {};
        std::vector<literal> failed_assumptions_ {};
        std::uint32_t trail_level_base_ {};
    };
}
