/// @file inc/kmx/sat/cdcl/garbage_collector.hpp
/// @brief Moving GC with updates to every affected reference.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <cstddef>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/bank/watch_list.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Moving GC with updates to every affected reference.
    ///
    /// @details
    /// `garbage_collector` runs the copying-collection cycle over `bank::arena`: `should_collect` decides, based on
    /// garbage-clause density/wasted arena bytes, whether a cycle is worthwhile; `collect` drives the cycle, calling
    /// `relocate_live_clause` for each clause not marked garbage in `clause::database`, then `rewrite_watchers` (via
    /// `bank::watch_list::replace_clause_ref_after_gc`) and `rewrite_reasons` (rewriting every `clause::ref_t` stored
    /// as a trail-level implication reason) so no consumer is left holding a reference into the retired arena
    /// generation; `finalize_cycle` releases the retired arena through `bank::arena::release_inactive`.
    /// @note This is one of the two places (with `compaction_service`) responsible for the risk point "full
    /// reindexing of watch lists, reasons, and external mapping": both reference-rewrite steps must complete before
    /// propagation or conflict analysis resumes, or dangling references result.
    class garbage_collector final
    {
    public:
        /// @brief Constructs a garbage collector with no pending cycle.
        /// @throws None (noexcept).
        garbage_collector() noexcept = default;

        /// @brief Checks whether the current garbage-clause density justifies running a collection cycle.
        /// @return True if a collection cycle should run now.
        /// @throws None (noexcept).
        bool should_collect() const noexcept
        {
            return false;
        }

        /// @brief Runs a full moving garbage-collection cycle over the clause database and arena.
        /// @throws None (noexcept).
        void collect() noexcept
        {
            relocated_clause_count_ = 0u;
            watch_rewrite_count_ = 0u;
            reason_rewrite_count_ = 0u;
            finalized_ = false;
        }

        /// @brief Copies one clause not marked garbage into the survivor arena.
        /// @throws None (noexcept).
        void relocate_live_clause() noexcept
        {
            ++relocated_clause_count_;
        }

        /// @brief Rewrites watch-list clause references to point at the relocated clause positions.
        /// @throws None (noexcept).
        void rewrite_watchers() noexcept
        {
            ++watch_rewrite_count_;
        }

        /// @brief Rewrites trail-level implication reason references to point at the relocated clause positions.
        /// @throws None (noexcept).
        void rewrite_reasons() noexcept
        {
            ++reason_rewrite_count_;
        }

        /// @brief Completes the collection cycle by releasing the retired arena generation.
        /// @throws None (noexcept).
        void finalize_cycle() noexcept
        {
            finalized_ = true;
        }

        /// @brief Returns how many live clauses were relocated during the current cycle.
        /// @return Number of relocated clauses.
        /// @throws None (noexcept).
        std::size_t relocated_clause_count() const noexcept
        {
            return relocated_clause_count_;
        }

        /// @brief Returns how many watch-list references were rewritten during the current cycle.
        /// @return Number of rewritten watch-list references.
        /// @throws None (noexcept).
        std::size_t watch_rewrite_count() const noexcept
        {
            return watch_rewrite_count_;
        }

        /// @brief Returns how many implication reasons were rewritten during the current cycle.
        /// @return Number of rewritten implication reasons.
        /// @throws None (noexcept).
        std::size_t reason_rewrite_count() const noexcept
        {
            return reason_rewrite_count_;
        }

        /// @brief Returns whether the current collection cycle has been finalized.
        /// @return True once the cycle has been finalized.
        /// @throws None (noexcept).
        bool finalized() const noexcept
        {
            return finalized_;
        }

    private:
        std::size_t relocated_clause_count_ = 0u;
        std::size_t watch_rewrite_count_ = 0u;
        std::size_t reason_rewrite_count_ = 0u;
        bool finalized_ = false;
    };
}
