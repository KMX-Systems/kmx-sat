/// @file inc/kmx/sat/cdcl/controller/reduce.hpp
/// @brief Learned-clause database management based on glue, activity, and usage.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <span>
#endif
#include <kmx/sat/counter.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>

#include <array>
#include <vector>

namespace kmx::sat::cdcl::controller
{
    /// @brief Learned-clause database management based on glue, activity, and usage.
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
            next_reduction_at_ = reduction_interval_;
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
            ranked_scratch_.clear();

            // Rank on a key computed once per clause rather than on a comparator that re-reads two clause
            // headers per comparison: with tens of thousands of candidates the sort was most of a reduction.
            database.iterate_redundant(
                [&](const clause::ref_t candidate) noexcept
                {
                    if (!candidate.valid() || database.is_reason_clause(candidate))
                        return;
                    const auto quality = database.quality_of(candidate);
                    // A clause that served as a reason since the last pass is not a candidate: usage since the
                    // last reduction is the one signal that separates the clauses a structured formula keeps
                    // coming back to from the ones it never touches again, and without it the pass threw away
                    // what `hole9` and `bmc-ibm-12` had to relearn a thousand times over.
                    if (quality.tier >= clause::database::default_tier && !retained_by_activity(quality) && quality.used_count == 0u)
                        ranked_scratch_.push_back(ranked_candidate {rank_key(quality), candidate.offset()});
                });

            std::sort(ranked_scratch_.begin(), ranked_scratch_.end(), [](const ranked_candidate& left, const ranked_candidate& right) noexcept {
                return left.key != right.key ? left.key > right.key : left.offset < right.offset;
            });
            last_candidates_.reserve(ranked_scratch_.size());
            for (const auto& ranked: ranked_scratch_)
                last_candidates_.push_back(ranked.offset);

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
                    continue;

                const auto tier = database.tier_of(candidate);
                const auto quality = database.quality_of(candidate);
                if (tier >= clause::database::default_tier && !retained_by_activity(quality))
                    last_candidates_.push_back(candidate.offset());
            }

            std::sort(last_candidates_.begin(), last_candidates_.end(), [&database](const auto left, const auto right) noexcept {
                return compare_subset_candidates(database, left, right);
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
                    continue;
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
                ++reduction_pass_count_;
            reduce_pending_ = false;
        }

        /// @brief Recomputes tier membership for the redundant set from each clause's current glue.
        /// @details The no-argument overload only closes out the pass; tiers themselves were never recomputed, so
        /// every learned clause kept the tier it was born with. That had two consequences: the glue a clause was
        /// eventually measured at never influenced whether it was kept, and anything the minimizer promoted was
        /// pushed below `default_tier` permanently, which excluded it from candidate selection for the rest of the
        /// solve. Classifying from glue on each pass is the standard three-tier policy: very low glue is the core
        /// worth keeping, mid glue is kept while it stays useful, and the rest is what reduction is for.
        /// @param database Clause database whose redundant clauses should be reclassified.
        void update_tiers(clause::database& database) noexcept
        {
            ++update_tiers_call_count_;
            if (reduce_pending_)
                ++reduction_pass_count_;
            reduce_pending_ = false;

            tier_scratch_.clear();
            database.iterate_redundant([this](const clause::ref_t ref) noexcept { tier_scratch_.push_back(ref); });
            for (const auto ref: tier_scratch_)
            {
                const auto quality = database.quality_of(ref);
                const auto tier = quality.glue <= core_glue_limit_       ? clause::database::tier_t {0}
                                  : quality.glue <= retained_glue_limit_ ? clause::database::default_tier
                                                                         : clause::database::lowest_tier;
                database.set_tier(ref, tier);
                // Usage is "since the last pass": a mid-glue clause that was used keeps one pass of grace, a
                // high-glue one must earn its place again before the next pass.
                const auto grace = quality.used_count != 0u && tier == clause::database::default_tier ? std::uint8_t {1} : std::uint8_t {0};
                database.set_used_count(ref, grace);
            }
        }

        /// @brief Sets the glue at or below which a learned clause is treated as core and never reduced.
        void set_core_glue_limit(const std::uint32_t glue) noexcept { core_glue_limit_ = glue; }

        /// @brief Sets the glue at or below which a learned clause is retained while it stays useful.
        void set_retained_glue_limit(const std::uint32_t glue) noexcept { retained_glue_limit_ = glue; }

        /// @brief Returns the number of candidates selected by the most recent reduction pass.
        /// @return Selected candidate count.
        /// @throws None (noexcept).
        counter_t last_selected_candidate_count() const noexcept { return last_selected_candidate_count_; }

        /// @brief Returns how many redundant clauses were marked for reduction by the latest pass.
        /// @return Reduced candidate count.
        /// @throws None (noexcept).
        counter_t reduced_candidates() const noexcept { return reduced_candidates_; }

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
        counter_t flushed_candidates() const noexcept { return flushed_candidates_; }

        /// @brief Requests that the next coordinator loop executes a reduction pass.
        /// @throws None (noexcept).
        void request_reduce() noexcept { reduce_pending_ = true; }

        /// @brief Counts a conflict and schedules a reduction pass when the interval has elapsed.
        /// @details The interval stays fixed. Growing schedules (CaDiCaL's arithmetic, a square-root variant) were
        /// measured against it on the same binary: they cut `hole9` and the IBM instances by half but cost the
        /// random held-out set 9-20%, because a random formula wants its learned set small and fresh. What the
        /// structured formulas actually needed was not fewer passes but protection of the clauses they keep using
        /// (`select_reduction_candidates`), which gave them the same gain at no cost on the random set.
        void tick_conflict() noexcept
        {
            ++conflict_count_;
            if (reduction_interval_ != 0u && conflict_count_ >= next_reduction_at_)
            {
                reduce_pending_ = true;
                next_reduction_at_ = conflict_count_ + reduction_interval_;
            }
        }

        void set_reduction_interval(const counter_t interval) noexcept
        {
            reduction_interval_ = interval;
            next_reduction_at_ = interval;
        }

        /// @brief Returns how many completed reduction passes have run.
        /// @return Number of completed reduction passes.
        /// @throws None (noexcept).
        counter_t reduction_pass_count() const noexcept { return reduction_pass_count_; }

    private:
        struct ranked_candidate final
        {
            std::uint64_t key {};
            clause::ref_t::offset_t offset {};
        };

        /// @brief Packs the retention ranking into one integer: higher sorts first, i.e. is reduced first.
        /// @details Field order matches `compare_candidates`: lower tier quality, then higher glue, then fewer
        /// uses, then lower activity, then larger size. Activity is bucketed to sixteen bits, which keeps its role
        /// as a tie-breaker while letting the whole key fit one word.
        static std::uint64_t rank_key(const clause::database::quality& quality) noexcept
        {
            const auto tier = static_cast<std::uint64_t>(std::min<std::uint32_t>(quality.tier, 3u));
            const auto glue = static_cast<std::uint64_t>(std::min<std::uint32_t>(quality.glue, 4095u));
            const auto unused = static_cast<std::uint64_t>(255u - std::min<std::uint32_t>(quality.used_count, 255u));
            const auto activity = quality.activity <= 0.0 ? 0.0 : std::min(quality.activity, 65535.0);
            const auto inactivity = static_cast<std::uint64_t>(65535u - static_cast<std::uint32_t>(activity));
            const auto size = static_cast<std::uint64_t>(std::min<std::uint32_t>(quality.size, 65535u));
            return (tier << 60u) | (glue << 48u) | (unused << 40u) | (inactivity << 24u) | (size << 8u);
        }

        static bool compare_subset_candidates(const clause::database& database, const std::uint64_t left_offset,
                                              const std::uint64_t right_offset) noexcept
        {
            const auto left = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(left_offset)});
            const auto right = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(right_offset)});
            if (left.glue != right.glue)
                return left.glue > right.glue;
            if (left.used_count != right.used_count)
                return left.used_count < right.used_count;
            if (left.activity != right.activity)
                return left.activity < right.activity;
            return left_offset < right_offset;
        }

        static bool compare_candidates(const clause::database& database, const std::uint64_t left_offset,
                                       const std::uint64_t right_offset) noexcept
        {
            const auto left = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(left_offset)});
            const auto right = database.quality_of(clause::ref_t {static_cast<clause::ref_t::offset_t>(right_offset)});
            if (left.tier != right.tier)
                return left.tier > right.tier;
            if (left.glue != right.glue)
                return left.glue > right.glue;
            if (left.used_count != right.used_count)
                return left.used_count < right.used_count;
            if (left.activity != right.activity)
                return left.activity < right.activity;
            if (left.size != right.size)
                return left.size > right.size;
            return left_offset < right_offset;
        }

        void evaluate_candidate(const clause::database& database, const clause::ref_t candidate) noexcept
        {
            if (!candidate.valid() || database.is_reason_clause(candidate))
                return;

            const auto tier = database.tier_of(candidate);
            const auto quality = database.quality_of(candidate);
            if (tier >= clause::database::default_tier && !retained_by_activity(quality))
                last_candidates_.push_back(candidate.offset());
        }

        bool retained_by_activity(const clause::database::quality& quality) const noexcept
        {
            return quality.glue <= 2u && quality.activity >= activity_retention_threshold_ && activity_retention_threshold_ > 0.0;
        }

        void trim_to_reduction_quota() noexcept
        {
            if (last_candidates_.empty() || reduction_fraction_percent_ >= 100u)
                return;

            const auto quota = std::max<std::size_t>(1u, (last_candidates_.size() * reduction_fraction_percent_ + 99u) / 100u);
            if (quota < last_candidates_.size())
                last_candidates_.resize(quota);
        }

        bool reduce_pending_ {};
        counter_t select_call_count_ {};
        counter_t reduce_call_count_ {};
        counter_t flush_call_count_ {};
        counter_t update_tiers_call_count_ {};
        counter_t reduction_pass_count_ {};
        counter_t conflict_count_ {};
        counter_t last_selected_candidate_count_ {};
        counter_t reduced_candidates_ {};
        counter_t flushed_candidates_ {};
        std::vector<std::uint64_t> last_candidates_ {};
        std::vector<ranked_candidate> ranked_scratch_ {};
        std::vector<clause::ref_t> tier_scratch_ {};
        std::uint32_t core_glue_limit_ {2u};
        std::uint32_t retained_glue_limit_ {6u};
        std::uint32_t reduction_fraction_percent_ {75u};
        double activity_retention_threshold_ {2.0};
        counter_t reduction_interval_ {};
        counter_t next_reduction_at_ {};
    };
}
