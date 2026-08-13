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
        static constexpr std::uint8_t redundant_flag {1u << 0u};
        static constexpr std::uint8_t garbage_flag {1u << 1u};
        static constexpr std::uint8_t reason_flag {1u << 2u};
        static constexpr std::uint8_t shrunken_flag {1u << 3u};

        header() noexcept = default;

        header(const std::uint32_t size, const std::uint32_t glue, const bool redundant, const bool garbage, const bool reason,
               const bool shrunken, const std::uint32_t tier, const std::uint32_t used_count) noexcept:
            size_ {size},
            glue_ {glue},
            tier_ {tier},
            used_count_ {used_count},
            flags_ {compose_flags(redundant, garbage, reason, shrunken)}
        {
        }

        std::uint32_t size() const noexcept { return size_; }

        std::uint32_t glue() const noexcept { return glue_; }

        bool redundant() const noexcept { return has_flag(redundant_flag); }

        bool garbage() const noexcept { return has_flag(garbage_flag); }

        bool reason() const noexcept { return has_flag(reason_flag); }

        bool shrunken() const noexcept { return has_flag(shrunken_flag); }

        std::uint32_t tier() const noexcept { return tier_; }

        std::uint32_t used_count() const noexcept { return used_count_; }

        bool is_redundant() const noexcept { return has_flag(redundant_flag); }

        bool is_active_reason() const noexcept { return has_flag(reason_flag); }

        bool is_satisfied_by_shrink() const noexcept { return has_flag(shrunken_flag); }

        void set_redundant(const bool redundant) noexcept { set_flag(redundant_flag, redundant); }

        void set_garbage(const bool garbage) noexcept { set_flag(garbage_flag, garbage); }

        void set_reason(const bool reason) noexcept { set_flag(reason_flag, reason); }

        void set_shrunken(const bool shrunken) noexcept { set_flag(shrunken_flag, shrunken); }

        void set_tier(const std::uint32_t tier) noexcept { tier_ = tier; }

        void increment_used_count() noexcept { ++used_count_; }

    private:
        static std::uint8_t compose_flags(const bool redundant, const bool garbage, const bool reason, const bool shrunken) noexcept
        {
            std::uint8_t flags {};
            if (redundant)
            {
                flags |= redundant_flag;
            }
            if (garbage)
            {
                flags |= garbage_flag;
            }
            if (reason)
            {
                flags |= reason_flag;
            }
            if (shrunken)
            {
                flags |= shrunken_flag;
            }
            return flags;
        }

        bool has_flag(const std::uint8_t flag) const noexcept { return (flags_ & flag) != 0u; }

        void set_flag(const std::uint8_t flag, const bool enabled) noexcept
        {
            if (enabled)
            {
                flags_ |= flag;
                return;
            }
            flags_ &= static_cast<std::uint8_t>(~flag);
        }

        std::uint32_t size_ {};
        std::uint32_t glue_ {};
        std::uint32_t tier_ {};
        std::uint32_t used_count_ {};
        std::uint8_t flags_ {};
    };
}
