/// @file inc/kmx/sat/cdcl/garbage_collector.hpp
/// @brief Moving GC with updates to every affected reference.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <unordered_map>
    #include <utility>
    #include <vector>
#endif
#include <cstddef>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Old-to-new clause references recorded by a collection pass.
    using relocation_list_t = std::vector<std::pair<clause::ref_t, clause::ref_t>>;

    /// @brief Moving GC with updates to every affected reference.
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
            return (clause_database_ != nullptr) && (clause_database_->stats_snapshot().garbage_count > 0u);
        }

        /// @brief Runs a full moving garbage-collection cycle over the clause database and arena.
        /// @throws None (noexcept).
        void collect() noexcept;

        /// @brief Copies one clause not marked garbage into the survivor arena.
        /// @throws None (noexcept).
        void relocate_live_clause() noexcept;

        /// @brief Rewrites watch-list clause references to point at the relocated clause positions.
        /// @throws None (noexcept).
        void rewrite_watchers() noexcept;

        /// @brief Rewrites trail-level implication reason references to point at the relocated clause positions.
        /// @throws None (noexcept).
        void rewrite_reasons() noexcept;

        /// @brief Attaches the clause database whose live clauses should be relocated during collection.
        /// @param clause_database Clause database participating in this GC cycle.
        /// @throws None (noexcept).
        void attach_database(clause::database& clause_database) noexcept { clause_database_ = &clause_database; }

        /// @brief Attaches the watch-list bank that must be rewritten after clause relocation.
        /// @param watch_list Watch-list bank participating in this GC cycle.
        /// @throws None (noexcept).
        void attach_watch_list(bank::watch_list& watch_list) noexcept { watch_list_ = &watch_list; }

        /// @brief Attaches the assignment store whose reason references must be rewritten after relocation.
        /// @param assignment_store Assignment store participating in this GC cycle.
        /// @throws None (noexcept).
        void attach_assignment_store(store::assignment& assignment_store) noexcept { assignment_store_ = &assignment_store; }

        /// @brief Attaches the proof manager whose stable clause identity map must survive relocation.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Attaches opt-in cold storage whose tracked references must follow clause relocation.
        void attach_cold_store(store::clause_cold& cold_store) noexcept { cold_store_ = &cold_store; }

        /// @brief Completes the collection cycle by releasing the retired arena generation.
        /// @throws None (noexcept).
        void finalize_cycle() noexcept { finalized_ = true; }

        /// @brief Returns how many live clauses were relocated during the current cycle.
        /// @return Number of relocated clauses.
        /// @throws None (noexcept).
        std::size_t relocated_clause_count() const noexcept { return relocated_clause_count_; }

        /// @brief Returns how many watch-list references were rewritten during the current cycle.
        /// @return Number of rewritten watch-list references.
        /// @throws None (noexcept).
        std::size_t watch_rewrite_count() const noexcept { return watch_rewrite_count_; }

        /// @brief Returns how many implication reasons were rewritten during the current cycle.
        /// @return Number of rewritten implication reasons.
        /// @throws None (noexcept).
        std::size_t reason_rewrite_count() const noexcept { return reason_rewrite_count_; }

        /// @brief Returns how many relocation notifications were sent to proof management.
        std::size_t proof_relocation_count() const noexcept { return proof_relocation_count_; }

        /// @brief Returns whether the current collection cycle has been finalized.
        /// @return True once the cycle has been finalized.
        /// @throws None (noexcept).
        bool finalized() const noexcept { return finalized_; }

    private:
        clause::database* clause_database_ {};
        bank::watch_list* watch_list_ {};
        store::assignment* assignment_store_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        store::clause_cold* cold_store_ {};
        std::vector<clause::ref_t> pending_live_refs_ {};
        relocation_list_t relocated_refs_ {};
        std::size_t relocated_clause_count_ {};
        std::size_t watch_rewrite_count_ {};
        std::size_t reason_rewrite_count_ {};
        std::size_t proof_relocation_count_ {};
        bool finalized_ {};
    };
}
