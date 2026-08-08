/// @file inc/kmx/sat/proof/clause/id.hpp
/// @brief Stable logical clause identity for proof/checking, separate from clause::ref_t.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <compare>
    #include <cstdint>
#endif

namespace kmx::sat::proof::clause
{
    /// @brief Stable logical clause identity for proof/checking, separate from clause::ref_t.
    ///
    /// Proof formats such as LRAT and FRAT require every clause to keep one stable identifier across its entire
    /// lifetime, independent of where the clause physically lives; `proof::clause::id` is that identifier, allocated
    /// by `proof::clause::id_allocator` when a clause is created and retired only when the clause is deleted, never
    /// reused meanwhile. This is what lets `garbage_collector` relocate a clause's `clause::ref_t` and
    /// `compaction_service` renumber variables without invalidating any proof antecedent chain that already refers to
    /// this clause by id.
    /// @note Only formats that need explicit antecedent bookkeeping (`lrat_tracer`, `frat_tracer`, and the checkers
    /// built on them) depend on ids being stable across every pass that is active while they are enabled; DRAT-only
    /// configurations do not require per-clause identity.
    class id final
    {
    public:
        using value_t = std::uint64_t;
        static constexpr value_t invalid_value {static_cast<value_t>(-1)};

        /// @brief Constructs an invalid clause identity.
        /// @throws None (noexcept).
        id() noexcept = default;
        /// @brief Constructs a clause identity from a raw allocator-issued value.
        /// @param value Identity value issued by `proof::clause::id_allocator`.
        /// @throws None (noexcept).
        explicit constexpr id(const value_t value) noexcept : value_ {value}
        {
        }

        /// @brief Returns the raw identity value.
        /// @return Underlying 64-bit identity value.
        /// @throws None (noexcept).
        constexpr value_t value() const noexcept
        {
            return value_;
        }

        /// @brief Checks whether this identity refers to a real, allocated clause rather than the invalid sentinel.
        /// @return True if this identity is valid.
        /// @throws None (noexcept).
        constexpr bool valid() const noexcept
        {
            return value_ != invalid_value;
        }

        /// @brief Compares this identity for equality against another.
        /// @param other Identity to compare against.
        /// @return True if both identities carry the same raw value.
        /// @throws None (noexcept).
        constexpr bool equals(const id& other) const noexcept
        {
            return value_ == other.value_;
        }

    private:
        value_t value_ {invalid_value};
    };
}
