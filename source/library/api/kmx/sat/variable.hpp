/// @file api/kmx/sat/variable.hpp
/// @brief Strong wrapper for a variable index; removes raw integer usage from the public hot path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <compare>
    #include <cstdint>
#endif

namespace kmx::sat
{
    /// @brief Strong wrapper for a variable index; removes raw integer usage from the public hot path.
    ///
    /// @details
    /// `variable` is a zero-cost, `constexpr`/`noexcept` strong index type used everywhere a raw integer variable
    /// number would otherwise be passed by value (clause construction, `literal` encoding, `variable_mapper`
    /// lookups, heuristic banks such as `vmtf_queue`/`evsids_heap`). Distinguishing it from `literal` at the type
    /// level prevents accidental mixing of signed/polarity-encoded values with plain variable indices, a common
    /// source of off-by-one and sign bugs in C-style SAT solver code.
    /// @note External variable numbering (as seen through the public API) and internal variable numbering (as used
    /// by `solver_core`) are related but distinct address spaces bridged by `variable_mapper`; a raw `index()` value
    /// is only meaningful relative to the mapping context it came from.
    class variable final
    {
    public:
        using index_t = std::uint32_t;

        /// @brief Constructs an invalid/zero-initialized variable (`index() == 0`).
        /// @throws None (noexcept).
        variable() noexcept = default;
        /// @brief Constructs a variable from a raw index value.
        /// @param value Variable index, interpreted relative to the caller's mapping context (external or internal).
        /// @throws None (noexcept).
        explicit constexpr variable(const index_t value) noexcept: index_ {value} {}

        /// @brief Returns the underlying raw index.
        /// @return Variable index value.
        /// @throws None (noexcept).
        constexpr index_t index() const noexcept { return index_; }

        /// @brief Compares two variables by their raw index.
        /// @return Ordering/equality result following the underlying index value.
        /// @throws None (noexcept).
        constexpr auto operator<=>(const variable&) const noexcept = default;

    private:
        index_t index_ {};
    };
}
