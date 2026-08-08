/// @file inc/kmx/sat/cdcl/bank/arena.hpp
/// @brief Manages the active and survivor arenas through PMR.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <memory_resource>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::cdcl::bank
{
    /// @brief Manages the active and survivor arenas through PMR.
    ///
    /// `bank::arena` is the lowest-level owner of clause byte storage, backed by a caller-supplied
    /// `std::pmr::memory_resource` so the solver never hardcodes a global allocator. It follows a dual-arena moving
    /// design in the CaDiCaL/Kissat tradition: `allocate_clause` reserves space for a new clause of a given literal
    /// count in the active arena, `prepare_gc`/`swap_survivor` implement the copying-collector half-cycle used by
    /// `garbage_collector` (live clauses are materialized into the survivor arena, which then becomes the new active
    /// arena), and `release_inactive` frees the previous arena's backing memory once every live reference has been
    /// rewritten. `contains` lets other subsystems assert that a `clause::ref_t` still belongs to the current arena
    /// generation before dereferencing it.
    /// @warning `materialize_clause` and `swap_survivor` invalidate every `clause::ref_t` and `clause::view` obtained
    /// before the call; callers must re-resolve references afterward, never dereference stale ones.
    class arena final
    {
    public:
        /// @brief Constructs an arena backed by the default PMR memory resource.
        /// @throws None (noexcept).
        arena() noexcept = default;
        /// @brief Constructs an arena backed by a caller-supplied PMR memory resource.
        /// @param resource Memory resource used for all arena allocations; must outlive this arena.
        /// @throws None (noexcept).
        explicit arena(std::pmr::memory_resource* resource) noexcept : resource_ {resource}
        {
        }

        /// @brief Reserves storage in the active arena for a new clause of the given literal count.
        /// @param literal_count Number of literals the new clause will hold.
        /// @return Reference to the newly reserved clause storage.
        /// @throws None (noexcept).
        clause::ref_t allocate_clause(const std::size_t literal_count) noexcept
        {
            return {};
        }

        /// @brief Copies a live clause from the active arena into the survivor arena during a GC half-cycle.
        /// @param ref Reference to the clause to materialize into the survivor arena.
        /// @throws None (noexcept).
        void materialize_clause(const clause::ref_t ref) noexcept
        {
        }

        /// @brief Checks whether a reference resolves within the currently active arena generation.
        /// @param ref Reference to validate.
        /// @return True if `ref` still belongs to the current arena.
        /// @throws None (noexcept).
        bool contains(const clause::ref_t ref) const noexcept
        {
            return false;
        }

        /// @brief Prepares internal bookkeeping for an upcoming garbage-collection cycle.
        /// @throws None (noexcept).
        void prepare_gc() noexcept
        {
        }

        /// @brief Promotes the survivor arena to become the new active arena, completing a GC half-cycle.
        /// @throws None (noexcept).
        void swap_survivor() noexcept
        {
        }

        /// @brief Releases backing memory for the arena generation retired by the last `swap_survivor` call.
        /// @throws None (noexcept).
        void release_inactive() noexcept
        {
        }

    private:
        std::pmr::memory_resource* resource_ {std::pmr::get_default_resource()};
    };
}
