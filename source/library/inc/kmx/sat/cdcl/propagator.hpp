/// @file inc/kmx/sat/cdcl/propagator.hpp
/// @brief BCP with two-watched literals and a blocking-literal fast path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/trail.hpp>

namespace kmx::sat::cdcl
{
    /// @brief BCP with two-watched literals and a blocking-literal fast path.
    /// @details
    /// `propagator` implements Boolean Constraint Propagation using the two-watched-literals scheme (Chaff/MiniSat
    /// lineage): each clause watches exactly two of its literals in `bank::watch_list`, and only an assignment to one
    /// of those two literals ever requires re-examining the clause. `propagate` is the main unit-propagation loop
    /// driven by `search_coordinator` on every trail advance; `attach_clause`/`detach_clause` establish or remove a
    /// clause's pair of watched literals (invoked when a clause is created, learned, or marked garbage);
    /// `watch_clause` registers one watch entry. `propagate_assumptions` runs propagation specifically for
    /// assumption-forced literals staged by `store::assumption`, and `propagate_beyond_conflict` continues
    /// propagating implied literals needed by proof/analysis bookkeeping after a conflicting clause has already been
    /// found.
    /// @note Per the hot-path micro-optimization directives, this is one of exactly three components (with
    /// `conflict_analyzer` and `bank::watch_list`) permitted explicit software prefetch and `[[likely]]`/`[[unlikely]]`
    /// branch annotation on measured hot loops (next watch entry/clause literals), and a candidate for optional
    /// explicit SIMD watch-list scanning with scalar fallback.
    /// @reference Two-watched-literal unit propagation (Moskewicz et al., "Chaff: Engineering an Efficient SAT
    /// Solver"), with the blocking-literal fast path popularized by MiniSat/CaDiCaL.
    class propagator final
    {
    public:
        /// @brief Constructs a propagator with empty watch-list state.
        /// @throws None (noexcept).
        propagator() noexcept = default;

        /// @brief Propagates all pending trail entries until fixpoint or a conflict is found.
        /// @return Reference to the conflicting clause, or an invalid reference if propagation reached fixpoint.
        /// @throws None (noexcept).
        clause::ref_t propagate() noexcept
        {
            ++propagation_call_count_;
            if (has_staged_conflicts())
                return consume_staged_conflict();
            return {};
        }

        /// @brief Propagates assumption-forced literals staged by `store::assumption` for the current episode.
        /// @return Reference to the conflicting clause, or an invalid reference if no conflict was found.
        /// @throws None (noexcept).
        clause::ref_t propagate_assumptions() noexcept
        {
            ++assumption_propagation_call_count_;

            if (pending_assumption_count_ == 0)
            {
                // No assumptions are pending for this episode: any currently staged conflict was not caused by
                // assumption processing (e.g. it was staged directly ahead of the regular search loop), so it
                // must not be misattributed here; leave it for `propagate` to discover in the normal loop.
                return {};
            }

            pending_assumption_count_ = 0;
            if (has_staged_conflicts())
                return consume_staged_conflict();
            return {};
        }

        /// @brief Continues propagating implied literals needed by proof/analysis bookkeeping after a conflict.
        /// @return Reference to a further conflicting clause, or an invalid reference if none is found.
        /// @throws None (noexcept).
        clause::ref_t propagate_beyond_conflict() noexcept
        {
            ++beyond_conflict_propagation_call_count_;
            if (has_staged_conflicts())
                return consume_staged_conflict();
            return {};
        }

        /// @brief Registers a clause's currently chosen pair of watched literals in the watch lists.
        /// @param ref Reference to the clause being watched.
        /// @throws None (noexcept).
        void watch_clause(const clause::ref_t ref) noexcept
        {
            if (!ref.valid() || is_watched(ref))
                return;
            watched_.push_back(ref);
        }

        /// @brief Removes a clause's watched-literal entries, typically before deletion or relocation.
        /// @param ref Reference to the clause being detached.
        /// @throws None (noexcept).
        void detach_clause(const clause::ref_t ref) noexcept
        {
            const auto it = std::find(watched_.begin(), watched_.end(), ref);
            if (it != watched_.end())
            {
                std::swap(*it, watched_.back());
                watched_.pop_back();
            }
        }

        /// @brief Selects and registers the initial pair of watched literals for a newly created clause.
        /// @param ref Reference to the clause being attached.
        /// @throws None (noexcept).
        void attach_clause(const clause::ref_t ref) noexcept { watch_clause(ref); }

        /// @brief Stages a conflict to be returned on the next propagation call.
        /// @param ref Reference to the clause to report as conflicting.
        /// @throws None (noexcept).
        void stage_conflict(const clause::ref_t ref) noexcept
        {
            if (!ref.valid())
                return;
            staged_conflicts_.push_back(ref);
        }

        /// @brief Returns the number of currently watched clauses.
        /// @return Number of watched clause references.
        /// @throws None (noexcept).
        std::size_t watched_clause_count() const noexcept { return watched_.size(); }

        /// @brief Returns whether a clause is currently attached for propagation.
        /// @param ref Reference to the clause to query.
        /// @return True if `ref` is present in the watched-clause set.
        [[nodiscard]] bool is_attached(const clause::ref_t ref) const noexcept { return is_watched(ref); }

        /// @brief Returns whether there are staged conflicts still pending for the next consume step.
        /// @return True if at least one staged conflict remains queued.
        [[nodiscard]] bool has_staged_conflicts() const noexcept { return staged_conflict_head_ < staged_conflicts_.size(); }

        /// @brief Returns the number of conflicts currently queued for the next propagation step.
        /// @return Number of staged conflicts waiting to be consumed.
        std::size_t staged_conflict_count() const noexcept
        {
            return staged_conflict_head_ < staged_conflicts_.size() ? staged_conflicts_.size() - staged_conflict_head_ : 0u;
        }

        /// @brief Returns the number of main propagation calls performed so far.
        /// @return Number of `propagate` calls.
        /// @throws None (noexcept).
        std::size_t propagation_call_count() const noexcept { return propagation_call_count_; }

        /// @brief Returns the number of assumption propagation calls performed so far.
        /// @return Number of `propagate_assumptions` calls.
        /// @throws None (noexcept).
        std::size_t assumption_propagation_call_count() const noexcept { return assumption_propagation_call_count_; }

        /// @brief Returns the number of post-conflict propagation calls performed so far.
        /// @return Number of `propagate_beyond_conflict` calls.
        /// @throws None (noexcept).
        std::size_t beyond_conflict_propagation_call_count() const noexcept { return beyond_conflict_propagation_call_count_; }

        /// @brief Sets how many assumptions are pending for the next assumption-propagation pass.
        /// @param count Number of assumptions staged for the episode.
        /// @throws None (noexcept).
        void set_pending_assumption_count(const std::size_t count) noexcept { pending_assumption_count_ = count; }

        /// @brief Clears episode-scoped propagation state before starting a fresh solve episode.
        /// @throws None (noexcept).
        void reset_episode_state() noexcept
        {
            staged_conflicts_.clear();
            staged_conflict_head_ = 0u;
            pending_assumption_count_ = 0u;
        }

    private:
        bool is_watched(const clause::ref_t ref) const noexcept
        {
            return std::find(watched_.begin(), watched_.end(), ref) != watched_.end();
        }

        clause::ref_t consume_staged_conflict() noexcept
        {
            if (!has_staged_conflicts())
                return {};

            const auto conflict = staged_conflicts_[staged_conflict_head_++];

            if (staged_conflict_head_ == staged_conflicts_.size())
            {
                staged_conflicts_.clear();
                staged_conflict_head_ = 0u;
            }
            else if (staged_conflict_head_ >= staged_conflict_compaction_threshold_)
            {
                staged_conflicts_.erase(staged_conflicts_.begin(),
                                        staged_conflicts_.begin() + static_cast<std::ptrdiff_t>(staged_conflict_head_));
                staged_conflict_head_ = 0u;
            }

            return conflict;
        }

        static constexpr std::size_t staged_conflict_compaction_threshold_ {64u};

        std::vector<clause::ref_t> watched_ {};
        std::vector<clause::ref_t> staged_conflicts_ {};
        std::size_t staged_conflict_head_ {};
        std::size_t propagation_call_count_ {};
        std::size_t assumption_propagation_call_count_ {};
        std::size_t beyond_conflict_propagation_call_count_ {};
        std::size_t pending_assumption_count_ {};
    };
}
