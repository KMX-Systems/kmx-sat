/// @file api/kmx/sat/literal.hpp
/// @brief Strong wrapper for literals; removes raw int usage from the public hot path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <compare>
    #include <cstdint>
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    /// @brief Strong wrapper for literals; removes raw int usage from the public hot path.
    ///
    /// A `literal` packs a `variable` index and its polarity into one `raw_t` value using the classic
    /// "index-times-two plus sign-bit" encoding (`(var.index() << 1) | negated`), the same scheme used by
    /// MiniSat-family solvers because it lets `index_in_watch_bank()` double as a dense, cache-friendly index into
    /// per-literal arrays (assignment values, watch lists) without a branch on sign. Because it is a literal type with
    /// only `constexpr`/`noexcept` operations and a defaulted three-way comparison, it can be used identically in
    /// `constexpr` contexts, as an unordered/ordered-container key, and on the propagation hot path.
    /// @warning `literal` carries no bounds information; validating that `variable_of()` stays within the currently
    /// declared/allocated variable range is the responsibility of the input-validation layer (`dimacs_parser`,
    /// `external_frontend`), not of this type.
    class literal final
    {
    public:
        using raw_t = std::uint32_t;

        /// @brief Constructs an invalid/zero-initialized literal (`raw() == 0`).
        /// @throws None (noexcept).
        literal() noexcept = default;
        /// @brief Constructs a literal directly from its packed representation.
        /// @param value Pre-encoded `raw_t` value using the `(index << 1) | negated` scheme.
        /// @throws None (noexcept).
        explicit constexpr literal(const raw_t value) noexcept: raw_ {value} {}
        /// @brief Constructs a literal from a variable and an explicit polarity.
        /// @param var Variable this literal refers to.
        /// @param negated True to construct the negative literal (`-var`), false for the positive literal.
        /// @throws None (noexcept).
        constexpr literal(const variable var, const bool negated) noexcept:
            raw_ {static_cast<raw_t>((var.index() << 1u) | (negated ? 1u : 0u))}
        {
        }

        /// @brief Returns the packed representation combining the variable index and polarity bit.
        /// @return Raw encoded literal value.
        /// @throws None (noexcept).
        constexpr raw_t raw() const noexcept { return raw_; }

        /// @brief Extracts the underlying variable, discarding polarity.
        /// @return Variable this literal refers to.
        /// @throws None (noexcept).
        constexpr variable variable_of() const noexcept { return variable {raw_ >> 1u}; }

        /// @brief Checks whether this literal is the negative occurrence of its variable.
        /// @return True if negated, false if positive.
        /// @throws None (noexcept).
        constexpr bool is_negated() const noexcept { return (raw_ & 1u) != 0u; }

        /// @brief Returns the complementary literal for the same variable.
        /// @return Literal with the same variable and inverted polarity.
        /// @throws None (noexcept).
        constexpr literal negated() const noexcept { return literal {raw_ ^ 1u}; }

        /// @brief Returns the dense index used to address per-literal arrays such as `bank::watch_list` slots.
        /// @return Index suitable for direct array indexing, distinct per literal polarity.
        /// @throws None (noexcept).
        constexpr raw_t index_in_watch_bank() const noexcept { return raw_; }

        /// @brief Compares two literals by their packed representation.
        /// @return Ordering/equality result following the packed `raw_t` value.
        /// @throws None (noexcept).
        constexpr auto operator<=>(const literal&) const noexcept = default;

    private:
        raw_t raw_ {};
    };
}
