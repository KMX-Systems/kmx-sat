/// @file inc/kmx/sat/cdcl/watch.hpp
/// @brief Compact element stored in watch lists.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Compact element stored in watch lists.
    /// @details
    /// A watch entry is eight bytes: the blocking literal and the clause reference, with the binary flag folded
    /// into the low bit of the reference (arena offsets are four-byte aligned, so that bit is always free). The
    /// propagation loop streams through watch lists more than through anything else, and halving the entry from
    /// the previous sixteen bytes halves the bytes it has to read per visit.
    ///
    /// For a binary clause the blocking literal *is* the other literal of the clause, which is why no separate
    /// field is needed: `binary_literal()` and `blocking_literal()` name the same value, and a binary entry never
    /// requires the clause body to be fetched. The clause reference is still carried so that conflict analysis can
    /// resolve on the clause like any other.
    class watch final
    {
    public:
        constexpr watch() noexcept = default;

        constexpr watch(const literal blocking, const clause::ref_t clause_ref, const bool is_binary = false) noexcept:
            blocking_ {blocking},
            tagged_ref_ {tag(clause_ref.offset(), is_binary)}
        {
        }

        [[nodiscard]] constexpr literal blocking_literal() const noexcept { return blocking_; }

        /// @brief Returns the other literal of a binary clause; identical to the blocking literal by construction.
        [[nodiscard]] constexpr literal binary_literal() const noexcept { return blocking_; }

        [[nodiscard]] constexpr bool is_binary() const noexcept { return (tagged_ref_ & binary_tag) != 0u; }

        [[nodiscard]] constexpr clause::ref_t clause_ref() const noexcept
        {
            const auto raw = tagged_ref_ & ~binary_tag;
            return clause::ref_t {(raw == untagged_invalid) ? clause::ref_t::invalid_offset : raw};
        }

        /// @brief Returns the clause offset without the invalid-sentinel fix-up; only for entries known to be valid.
        [[nodiscard]] constexpr clause::ref_t::offset_t raw_offset() const noexcept { return tagged_ref_ & ~binary_tag; }

        constexpr void set_blocking_literal(const literal lit) noexcept { blocking_ = lit; }

        /// @brief Sets the other literal of a binary clause, which is the blocking literal.
        constexpr void set_binary_literal(const literal lit) noexcept { blocking_ = lit; }

        constexpr void set_clause_ref(const clause::ref_t clause_ref) noexcept { tagged_ref_ = tag(clause_ref.offset(), is_binary()); }

        /// @brief Compares two watch entries by the clause they identify.
        /// @return True if both entries refer to the same clause.
        [[nodiscard]] constexpr bool operator==(const watch& other) const noexcept { return raw_offset() == other.raw_offset(); }

    private:
        static constexpr std::uint32_t binary_tag {1u};
        static constexpr std::uint32_t untagged_invalid {clause::ref_t::invalid_offset & ~binary_tag};

        static constexpr std::uint32_t tag(const clause::ref_t::offset_t offset, const bool is_binary) noexcept
        {
            return (offset & ~binary_tag) | (is_binary ? binary_tag : 0u);
        }

        literal blocking_ {};
        std::uint32_t tagged_ref_ {untagged_invalid};
    };

    static_assert(sizeof(watch) == 8u);
}
