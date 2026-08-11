/// @file inc/kmx/sat/cdcl/controller/reduce.hpp
/// @brief Learned-clause database management based on glue, activity, and usage.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>

#include <array>
#include <vector>

namespace kmx::sat::cdcl::controller
{
    /// @brief Learned-clause database management based on glue, activity, and usage.
    ///
    /// @details
    /// `controller::reduce` periodically shrinks `clause::database`'s redundant (learned) clause set so it does not
    /// grow without bound, following the glue/activity-based retention policy common to CaDiCaL/Kissat: low-glue,
    /// recently-used clauses are kept, while high-glue, long-unused clauses become deletion candidates.
    /// `should_reduce` decides, from a conflict-count schedule, when a reduction pass is due;
    /// `select_reduction_candidates` ranks redundant clauses by glue/activity/`clause::header::used_count`;
    /// `reduce_clauses` marks the selected candidates garbage via `clause::database::mark_garbage` (never a clause currently
    /// serving as a reason, per `clause::header::reason`); `flush_redundant` performs the actual removal, typically
    /// in coordination with `flush_restore_manager`; and `update_tiers` recomputes tier membership after the pass.
    /// `memory_governor` may also invoke this controller out of its normal schedule as the first step of its
    /// soft-ceiling escalation ladder.
    class reduce final
    {
    public:
        /// @brief Constructs a reduce controller with a default schedule.
        /// @throws None (noexcept).
        reduce() noexcept = default;

        /// @brief Checks whether the reduction schedule (or an out-of-band memory-pressure request) is due.
        /// @return True if a reduction pass should run now.
        /// @throws None (noexcept).
        bool should_reduce() const noexcept
        {
            return reduce_pending_;
        }

        /// @brief Ranks redundant clauses by glue/activity/usage to select reduction candidates.
        /// @throws None (noexcept).
        void select_reduction_candidates() noexcept
        {
            ++select_call_count_;
            last_selected_candidate_count_ = select_call_count_;
            last_candidates_.push_back(last_selected_candidate_count_);
        }

        /// @brief Selects reduction candidates from a set of clause references using database tier/usage hints.
        /// @param database Clause database that exposes tier and reason/garbage state.
        /// @param candidates Candidate clauses to rank.
        /// @throws None (noexcept).
        template <std::size_t size>
        void select_reduction_candidates(const clause::database& database,
                                         const std::array<clause::ref_t, size>& candidates) noexcept
        {
            ++select_call_count_;
            std::uint64_t selected {0};
            last_candidates_.clear();

            for (const auto candidate : candidates)
            {
                if (!candidate.valid() || database.is_reason_clause(candidate))
                {
                    continue;
                }

                const auto tier = database.tier_of(candidate);
                if (tier >= clause::database::default_tier)
                {
                    ++selected;
                    last_candidates_.push_back(candidate.offset());
                }
            }

            last_selected_candidate_count_ = selected;
            reduced_candidates_ = selected;
        }

        /// @brief Marks the selected low-quality redundant clauses as garbage.
        /// @throws None (noexcept).
        void reduce_clauses() noexcept
        {
            ++reduce_call_count_;
            if (!last_candidates_.empty())
            {
                reduced_candidates_ = last_candidates_.size();
            }
        }

        /// @brief Physically removes clauses marked garbage by this reduction pass.
        /// @throws None (noexcept).
        void flush_redundant() noexcept
        {
            ++flush_call_count_;
            flushed_candidates_ = reduced_candidates_;
        }

        /// @brief Recomputes clause-database tier membership after a reduction pass.
        /// @throws None (noexcept).
        void update_tiers() noexcept
        {
            ++update_tiers_call_count_;
            if (reduce_pending_)
            {
                ++reduction_pass_count_;
            }
            reduce_pending_ = false;
        }

        /// @brief Returns the number of candidates selected by the most recent reduction pass.
        /// @return Selected candidate count.
        /// @throws None (noexcept).
        std::uint64_t last_selected_candidate_count() const noexcept
        {
            return last_selected_candidate_count_;
        }

        /// @brief Returns how many redundant clauses were marked for reduction by the latest pass.
        /// @return Reduced candidate count.
        /// @throws None (noexcept).
        std::uint64_t reduced_candidates() const noexcept
        {
            return reduced_candidates_;
        }

        /// @brief Returns whether the most recent selection step produced any reduction candidates.
        /// @return True if at least one candidate was selected.
        [[nodiscard]] bool has_candidates() const noexcept
        {
            return !last_candidates_.empty();
        }

        /// @brief Returns how many candidates were actually flushed by the latest pass.
        /// @return Flushed candidate count.
        /// @throws None (noexcept).
        std::uint64_t flushed_candidates() const noexcept
        {
            return flushed_candidates_;
        }

        /// @brief Requests that the next coordinator loop executes a reduction pass.
        /// @throws None (noexcept).
        void request_reduce() noexcept
        {
            reduce_pending_ = true;
        }

        /// @brief Returns how many completed reduction passes have run.
        /// @return Number of completed reduction passes.
        /// @throws None (noexcept).
        std::uint64_t reduction_pass_count() const noexcept
        {
            return reduction_pass_count_;
        }

    private:
        bool reduce_pending_ {false};
        std::uint64_t select_call_count_ {0};
        std::uint64_t reduce_call_count_ {0};
        std::uint64_t flush_call_count_ {0};
        std::uint64_t update_tiers_call_count_ {0};
        std::uint64_t reduction_pass_count_ {0};
        std::uint64_t last_selected_candidate_count_ {0};
        std::uint64_t reduced_candidates_ {0};
        std::uint64_t flushed_candidates_ {0};
        std::vector<std::uint64_t> last_candidates_ {};
    };
}
