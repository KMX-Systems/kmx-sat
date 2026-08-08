/// @file inc/kmx/sat/cdcl/bank/watch_list.hpp
/// @brief All watch lists, partitioned by literal.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::bank
{
    /// @brief All watch lists, partitioned by literal.
    ///
    /// `bank::watch_list` holds one list of `watch` entries per literal (positive and negative occurrence of every
    /// variable get independent lists), the core data structure `propagator::propagate` scans on every unit
    /// propagation step. `watch_literal`/`unwatch_literal` add/remove one watch entry, invoked when
    /// `propagator::attach_clause`/`detach_clause` change which two literals of a clause are watched.
    /// `replace_clause_ref_after_gc` rewrites every stored `clause::ref_t` after `garbage_collector::collect`
    /// relocates clauses, and `reindex_after_compaction` rebuilds the per-literal partition after
    /// `compaction_service` renumbers variables; both must run before propagation resumes, since a stale reference in
    /// a watch list would otherwise dereference relocated or renumbered storage. `flush_large_watches` prunes watch
    /// entries for clauses that have been marked garbage to keep list scans short.
    /// @warning A `watch_list` left un-rewritten after garbage collection or compaction contains dangling
    /// `clause::ref_t` values; both rewrite steps are mandatory, not optional, cleanup after those operations.
    class watch_list final
    {
    public:
        /// @brief Constructs an empty set of watch lists.
        /// @throws None (noexcept).
        watch_list() noexcept = default;

        /// @brief Adds one watch entry to the list for the given literal.
        /// @param lit Literal whose list receives the new entry.
        /// @param entry Watch entry to add.
        /// @throws None (noexcept).
        void watch_literal(const literal lit, const watch entry) noexcept
        {
        }

        /// @brief Removes one watch entry from the list for the given literal.
        /// @param lit Literal whose list loses the entry.
        /// @param entry Watch entry to remove.
        /// @throws None (noexcept).
        void unwatch_literal(const literal lit, const watch entry) noexcept
        {
        }

        /// @brief Visits every watch entry currently registered for the given literal.
        /// @param lit Literal whose watch list is visited.
        /// @throws None (noexcept).
        void iterate(const literal lit) const noexcept
        {
        }

        /// @brief Rewrites every stored clause reference after a garbage-collection cycle relocates clauses.
        /// @throws None (noexcept).
        void replace_clause_ref_after_gc() noexcept
        {
        }

        /// @brief Rebuilds the per-literal partition after `compaction_service` renumbers variables.
        /// @throws None (noexcept).
        void reindex_after_compaction() noexcept
        {
        }

        /// @brief Prunes watch entries referring to clauses already marked garbage, shortening future list scans.
        /// @throws None (noexcept).
        void flush_large_watches() noexcept
        {
        }
    };
}
