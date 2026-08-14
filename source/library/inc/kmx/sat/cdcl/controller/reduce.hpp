/// @file inc/kmx/sat/cdcl/controller/reduce.hpp
/// @brief Learned-clause database management based on glue, activity, and usage.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <span>
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
        bool should_reduce() const noexcept { return reduce_pending_; }

        /// @brief Resets reduction pending state and pass-local counters for a fresh solve episode.
        /// @throws None (noexcept).
        void reset() noexcept
        {
            reduce_pending_ = false;
            conflict_count_ = 0u;
            select_call_count_ = 0u;
            reduce_call_count_ = 0u;
            flush_call_count_ = 0u;
            update_tiers_call_count_ = 0u;
            reduction_pass_count_ = 0u;
            last_selected_candidate_count_ = 0u;
            reduced_candidates_ = 0u;
            flushed_candidates_ = 0u;
            last_candidates_.clear();
        }

        /// @brief Ranks redundant clauses by glue/activity/usage to select reduction candidates.
        /// @throws None (noexcept).
        void select_reduction_candidates() noexcept
        {
            ++select_call_count_;
            last_selected_candidate_count_ = 0u;
            reduced_candidates_ = 0u;
            last_candidates_.clear();
        }

        /// @brief Selects reduction candidates by scanning the current redundant set in the clause database.
        /// @param database Clause database providing tier and reason/garbage state.
        /// @throws None (noexcept).
        void select_reduction_candidates(const clause::database& database) noexcept
        {
            ++select_call_count_;
            last_candidates_.clear();

            database.iterate_redundant(
                [&](const clause::ref_t candidate) noexcept
                {
                    if (!candidate.valid() || database.is_reason_clause(candidate))
                    {
                        return;
                    }

                    const auto tier = database.tier_of(candidate);
                    const auto quality = database.quality_of(candidate);
                    if (tier >= clause::database::default_tier && !retained_by_activity(quality))
                    {
                        last_candidates_.push_back(candidate.offset());
                    }
                });

            std::sort(last_candidates_.begin(), last_candidates_.end(),
                      [&database](const auto left_offset, const auto right_offset) noexcept
                      {
                          const auto left = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(left_offset)});
                          const auto right = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(right_offset)});
                          if (left.tier != right.tier)
                          {
                              return left.tier > right.tier;
                          }
                          if (left.glue != right.glue)
                          {
                              return left.glue > right.glue;
                          }
                          if (left.used_count != right.used_count)
                          {
                              return left.used_count < right.used_count;
                          }
                          if (left.activity != right.activity)
                          {
                              return left.activity < right.activity;
                          }
                          if (left.size != right.size)
                          {
                              return left.size > right.size;
                          }
                          return left_offset < right_offset;
                      });

            last_selected_candidate_count_ = last_candidates_.size();
            trim_to_reduction_quota();
            last_selected_candidate_count_ = last_candidates_.size();
            reduced_candidates_ = last_selected_candidate_count_;
        }

        /// @brief Selects reduction candidates from a set of clause references using database tier/usage hints.
        /// @param database Clause database that exposes tier and reason/garbage state.
        /// @param candidates Candidate clauses to rank.
        /// @throws None (noexcept).
        template <std::size_t size>
        void select_reduction_candidates(const clause::database& database, const std::array<clause::ref_t, size>& candidates) noexcept
        {
            ++select_call_count_;
            last_candidates_.clear();

            for (const auto candidate: candidates)
            {
                if (!candidate.valid() || database.is_reason_clause(candidate))
                {
                    continue;
                }

                const auto tier = database.tier_of(candidate);
                const auto quality = database.quality_of(candidate);
                if (tier >= clause::database::default_tier && !retained_by_activity(quality))
                {
                    last_candidates_.push_back(candidate.offset());
                }
            }

            std::sort(last_candidates_.begin(), last_candidates_.end(),
                      [&database](const auto left_offset, const auto right_offset) noexcept
                      {
                          const auto left = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(left_offset)});
                          const auto right = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(right_offset)});
                          if (left.glue != right.glue)
                          {
                              return left.glue > right.glue;
                          }
                          if (left.used_count != right.used_count)
                          {
                              return left.used_count < right.used_count;
                          }
                          if (left.activity != right.activity)
                          {
                              return left.activity < right.activity;
                          }
                          return left_offset < right_offset;
                      });

            trim_to_reduction_quota();
            last_selected_candidate_count_ = last_candidates_.size();
            reduced_candidates_ = last_selected_candidate_count_;
        }

        /// @brief Marks the selected low-quality redundant clauses as garbage.
        /// @throws None (noexcept).
        void reduce_clauses() noexcept
        {
            ++reduce_call_count_;
            reduced_candidates_ = last_candidates_.size();
        }

        /// @brief Marks selected redundant clauses as garbage in the provided clause database.
        /// @param database Clause database to mutate.
        /// @throws None (noexcept).
        void reduce_clauses(clause::database& database) noexcept
        {
            ++reduce_call_count_;
            reduced_candidates_ = 0u;

            for (const auto offset: last_candidates_)
            {
                const auto ref = clause::ref_t {static_cast<clause::ref_t::offset_t>(offset)};
                if (!ref.valid() || database.is_reason_clause(ref) || database.is_garbage(ref))
                {
                    continue;
                }
                database.mark_garbage(ref);
                ++reduced_candidates_;
            }
        }

        /// @brief Physically removes clauses marked garbage by this reduction pass.
        /// @throws None (noexcept).
        void flush_redundant() noexcept
        {
            ++flush_call_count_;
            flushed_candidates_ = reduced_candidates_;
        }

        /// @brief Physically removes clauses marked garbage from the provided clause database.
        /// @param database Clause database whose garbage clauses should be removed.
        /// @throws None (noexcept).
        void flush_redundant(clause::database& database) noexcept
        {
            ++flush_call_count_;
            const auto before = database.stats_snapshot().redundant_count;
            database.flush_satisfied([&](const clause::ref_t ref) noexcept { return database.is_garbage(ref); });
            const auto after = database.stats_snapshot().redundant_count;
            flushed_candidates_ = (before >= after) ? (before - after) : 0u;
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
        std::uint64_t last_selected_candidate_count() const noexcept { return last_selected_candidate_count_; }

        /// @brief Returns how many redundant clauses were marked for reduction by the latest pass.
        /// @return Reduced candidate count.
        /// @throws None (noexcept).
        std::uint64_t reduced_candidates() const noexcept { return reduced_candidates_; }

        /// @brief Returns whether the most recent selection step produced any reduction candidates.
        /// @return True if at least one candidate was selected.
        [[nodiscard]] bool has_candidates() const noexcept { return !last_candidates_.empty(); }

        /// @brief Returns the most recent quality-ranked candidate offsets.
        /// @return Read-only candidate ordering from worst to best retention quality.
        std::span<const std::uint64_t> candidate_offsets() const noexcept { return last_candidates_; }

        /// @brief Sets the percentage of ranked candidates eligible for deletion in each reduction pass.
        /// @param percent Percentage in the inclusive range [1, 100].
        void set_reduction_fraction_percent(const std::uint32_t percent) noexcept
        {
            reduction_fraction_percent_ = std::clamp(percent, 1u, 100u);
        }

        /// @brief Returns the configured reduction quota percentage.
        std::uint32_t reduction_fraction_percent() const noexcept { return reduction_fraction_percent_; }

        /// @brief Sets the activity threshold protecting low-glue clauses from deletion.
        void set_activity_retention_threshold(const double threshold) noexcept
        {
            activity_retention_threshold_ = threshold < 0.0 ? 0.0 : threshold;
        }

        /// @brief Returns the activity threshold protecting low-glue clauses.
        double activity_retention_threshold() const noexcept { return activity_retention_threshold_; }

        /// @brief Returns how many candidates were actually flushed by the latest pass.
        /// @return Flushed candidate count.
        /// @throws None (noexcept).
        std::uint64_t flushed_candidates() const noexcept { return flushed_candidates_; }

        /// @brief Requests that the next coordinator loop executes a reduction pass.
        /// @throws None (noexcept).
        void request_reduce() noexcept { reduce_pending_ = true; }

        void tick_conflict() noexcept
        {
            ++conflict_count_;
            if (reduction_interval_ != 0u && (conflict_count_ % reduction_interval_) == 0u)
            {
                reduce_pending_ = true;
            }
        }

        void set_reduction_interval(const std::uint64_t interval) noexcept { reduction_interval_ = interval; }

        /// @brief Returns how many completed reduction passes have run.
        /// @return Number of completed reduction passes.
        /// @throws None (noexcept).
        std::uint64_t reduction_pass_count() const noexcept { return reduction_pass_count_; }

    private:
        bool retained_by_activity(const clause::database::quality& quality) const noexcept
        {
            return quality.glue <= 2u && quality.activity >= activity_retention_threshold_ && activity_retention_threshold_ > 0.0;
        }

        void trim_to_reduction_quota() noexcept
        {
            if (last_candidates_.empty() || reduction_fraction_percent_ >= 100u)
            {
                return;
            }

            const auto quota = std::max<std::size_t>(1u, (last_candidates_.size() * reduction_fraction_percent_ + 99u) / 100u);
            if (quota < last_candidates_.size())
            {
                last_candidates_.resize(quota);
            }
        }

        bool reduce_pending_ {};
        std::uint64_t select_call_count_ {};
        std::uint64_t reduce_call_count_ {};
        std::uint64_t flush_call_count_ {};
        std::uint64_t update_tiers_call_count_ {};
        std::uint64_t reduction_pass_count_ {};
        std::uint64_t conflict_count_ {};
        std::uint64_t last_selected_candidate_count_ {};
        std::uint64_t reduced_candidates_ {};
        std::uint64_t flushed_candidates_ {};
        std::vector<std::uint64_t> last_candidates_ {};
        std::uint32_t reduction_fraction_percent_ {50u};
        double activity_retention_threshold_ {2.0};
        std::uint64_t reduction_interval_ {};
    };
}
