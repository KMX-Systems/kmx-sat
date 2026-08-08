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
    ///
    /// `header` is the fixed-size metadata record physically stored alongside a clause's literals in `clause::storage`
    /// (in the CaDiCaL/Kissat tradition of packing size, glue, and flag bits tightly next to the literal array for
    /// cache locality). `glue()` (literals blocks distance / LBD) and `tier()` drive `reduce_controller`'s retention
    /// decisions; `redundant()` distinguishes learned clauses from original problem clauses; `garbage()` marks a
    /// clause pending physical reclamation by `garbage_collector`; `reason()` marks a clause currently serving as an
    /// implication reason on the trail and therefore ineligible for deletion or relocation without reason-pointer
    /// rewriting; `shrunken()` marks a clause reduced in place by `otf_strengthener`/`vivifier`; `used_count()`
    /// feeds activity-based reduction heuristics.
    /// @warning A clause with `reason() == true` must never be deleted or garbage-collected in place; the `reason`
    /// flag exists specifically so `garbage_collector`/`reduce_controller` can detect and skip such clauses to avoid
    /// dangling reason references on the trail.
    class header final
    {
    public:
        /// @brief Constructs a header describing an empty, non-redundant, non-garbage clause.
        /// @throws None (noexcept).
        header() noexcept = default;

        /// @brief Returns the number of literals in the clause.
        /// @return Literal count.
        /// @throws None (noexcept).
        std::uint32_t size() const noexcept
        {
            return {};
        }

        /// @brief Returns the glue (literal blocks distance) quality metric used for reduction and tiering decisions.
        /// @return Glue value; lower generally means higher quality for a learned clause.
        /// @throws None (noexcept).
        std::uint32_t glue() const noexcept
        {
            return {};
        }

        /// @brief Checks whether this clause is a learned (redundant) clause rather than an original problem clause.
        /// @return True if redundant/learned.
        /// @throws None (noexcept).
        bool redundant() const noexcept
        {
            return {};
        }

        /// @brief Checks whether this clause is marked for physical reclamation by the garbage collector.
        /// @return True if pending collection.
        /// @throws None (noexcept).
        bool garbage() const noexcept
        {
            return {};
        }

        /// @brief Checks whether this clause currently serves as an implication reason on the trail.
        /// @return True if currently in use as a reason clause.
        /// @throws None (noexcept).
        bool reason() const noexcept
        {
            return {};
        }

        /// @brief Checks whether this clause has been shrunk in place by strengthening/vivification.
        /// @return True if shrunk from its originally learned size.
        /// @throws None (noexcept).
        bool shrunken() const noexcept
        {
            return {};
        }

        /// @brief Returns the clause-database tier this clause currently belongs to, used by `reduce_controller` to
        /// separate high- from low-quality learned clauses.
        /// @return Tier index.
        /// @throws None (noexcept).
        std::uint32_t tier() const noexcept
        {
            return {};
        }

        /// @brief Returns how many times this clause has been used (for example as a propagation reason) since it was
        /// last considered for reduction.
        /// @return Usage counter value.
        /// @throws None (noexcept).
        std::uint32_t used_count() const noexcept
        {
            return {};
        }
    };
}
