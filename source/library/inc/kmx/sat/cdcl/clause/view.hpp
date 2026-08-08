/// @file inc/kmx/sat/cdcl/clause/view.hpp
/// @brief Safe clause view with strictly controlled lifetime.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
#endif
#include <kmx/sat/cdcl/clause/header.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief Safe clause view with strictly controlled lifetime.
    ///
    /// `clause::view` is the read-only, non-owning way every consumer (propagation, conflict analysis, proof tracers,
    /// simplification passes) inspects a clause's literals and `header` metadata without holding a raw pointer into
    /// `bank::arena`. Because arena storage may be relocated by `garbage_collector` or renumbered by
    /// `compaction_service`, a `view` must be obtained fresh from `clause::database`/`clause::storage` for each use
    /// rather than cached across an operation that could trigger relocation.
    /// @warning Retaining a `view` across a call that may invoke the garbage collector, compaction, or
    /// `clause::storage::shrink_clause`/`relocate_clause` is unsafe; re-resolve the view from its `clause::ref_t`
    /// afterward instead.
    class view final
    {
    public:
        /// @brief Constructs an empty clause view.
        /// @throws None (noexcept).
        view() noexcept = default;

        /// @brief Exposes the clause's literals.
        /// @return Read-only span over the clause's current literals.
        /// @throws None (noexcept).
        std::span<const literal> literals() const noexcept
        {
            return {};
        }

        /// @brief Returns a copy of this clause's compact metadata record.
        /// @return Header describing size, glue, and flag state.
        /// @throws None (noexcept).
        header header() const noexcept
        {
            return {};
        }

        /// @brief Checks whether the clause contains a given literal.
        /// @param lit Literal to search for.
        /// @return True if `lit` occurs in this clause.
        /// @throws None (noexcept).
        bool contains(const literal lit) const noexcept
        {
            return false;
        }

        /// @brief Checks whether this clause has exactly two literals.
        /// @return True if the clause is binary.
        /// @throws None (noexcept).
        bool is_binary() const noexcept
        {
            return false;
        }

        /// @brief Checks whether this clause has exactly one literal.
        /// @return True if the clause is a unit clause.
        /// @throws None (noexcept).
        bool is_unit() const noexcept
        {
            return false;
        }

        /// @brief Returns the number of literals in the clause.
        /// @return Literal count.
        /// @throws None (noexcept).
        std::uint32_t size() const noexcept
        {
            return {};
        }
    };
}
