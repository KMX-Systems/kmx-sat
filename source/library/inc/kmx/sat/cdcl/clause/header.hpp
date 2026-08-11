/// @file inc/kmx/sat/cdcl/clause/header.hpp
/// @brief Compact metadata for all clauses.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::cdcl::clause
{
    /// @brief Compact metadata for all clauses.
    /// @details
    /// `header` stores per-clause attributes used by reduction, conflict analysis, and proof accounting, including
    /// size, glue, redundant/garbage/reason flags, shrink marker, tier, and recent-usage count.
    class header final
    {
    public:
        header() noexcept = default;

        header(const std::uint32_t size, const std::uint32_t glue, const bool redundant, const bool garbage,
               const bool reason, const bool shrunken, const std::uint32_t tier, const std::uint32_t used_count) noexcept :
            size_ {size}, glue_ {glue}, redundant_ {redundant}, garbage_ {garbage}, reason_ {reason},
            shrunken_ {shrunken}, tier_ {tier}, used_count_ {used_count}
        {
        }

        std::uint32_t size() const noexcept
        {
            return size_;
        }

        std::uint32_t glue() const noexcept
        {
            return glue_;
        }

        bool redundant() const noexcept
        {
            return redundant_;
        }

        bool garbage() const noexcept
        {
            return garbage_;
        }

        bool reason() const noexcept
        {
            return reason_;
        }

        bool shrunken() const noexcept
        {
            return shrunken_;
        }

        std::uint32_t tier() const noexcept
        {
            return tier_;
        }

        std::uint32_t used_count() const noexcept
        {
            return used_count_;
        }

        bool is_redundant() const noexcept
        {
            return redundant_;
        }

        bool is_active_reason() const noexcept
        {
            return reason_;
        }

        bool is_satisfied_by_shrink() const noexcept
        {
            return shrunken_;
        }

        void set_redundant(const bool redundant) noexcept
        {
            redundant_ = redundant;
        }

        void set_garbage(const bool garbage) noexcept
        {
            garbage_ = garbage;
        }

        void set_reason(const bool reason) noexcept
        {
            reason_ = reason;
        }

        void set_shrunken(const bool shrunken) noexcept
        {
            shrunken_ = shrunken;
        }

        void set_tier(const std::uint32_t tier) noexcept
        {
            tier_ = tier;
        }

        void increment_used_count() noexcept
        {
            ++used_count_;
        }

    private:
        std::uint32_t size_ {0};
        std::uint32_t glue_ {0};
        bool redundant_ {false};
        bool garbage_ {false};
        bool reason_ {false};
        bool shrunken_ {false};
        std::uint32_t tier_ {0};
        std::uint32_t used_count_ {0};
    };
}
