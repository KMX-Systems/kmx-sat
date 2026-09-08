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
#include <kmx/sat/counter.hpp>

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
        void reset() noexcept;

        /// @brief Ranks redundant clauses by glue/activity/usage to select reduction candidates.
        /// @throws None (noexcept).
        void select_reduction_candidates() noexcept;

        /// @brief Selects reduction candidates by scanning the current redundant set in the clause database.
        /// @param database Clause database providing tier and reason/garbage state.
        /// @throws None (noexcept).
        void select_reduction_candidates(const clause::database& database) noexcept;

        /// @brief Selects reduction candidates from a set of clause references using database tier/usage hints.
        /// @param database Clause database that exposes tier and reason/garbage state.
        /// @param candidates Candidate clauses to rank.
        /// @throws None (noexcept).
        template <std::size_t Size>
        void select_reduction_candidates(const clause::database& database, const std::array<clause::ref_t, Size>& candidates) noexcept;

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
        void reduce_clauses(clause::database& database) noexcept;

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
        void flush_redundant(clause::database& database) noexcept;

        /// @brief Recomputes clause-database tier membership after a reduction pass.
        /// @throws None (noexcept).
        void update_tiers() noexcept;

        /// @brief Recomputes tier membership for the redundant set from each clause's current glue.
        /// @details The no-argument overload only closes out the pass; tiers themselves were never recomputed, so
        /// every learned clause kept the tier it was born with. That had two consequences: the glue a clause was
        /// eventually measured at never influenced whether it was kept, and anything the minimizer promoted was
        /// pushed below `default_tier` permanently, which excluded it from candidate selection for the rest of the
        /// solve. Classifying from glue on each pass is the standard three-tier policy: very low glue is the core
        /// worth keeping, mid glue is kept while it stays useful, and the rest is what reduction is for.
        /// @param database Clause database whose redundant clauses should be reclassified.
        void update_tiers(clause::database& database) noexcept;

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
            activity_retention_threshold_ = (threshold < 0.0) ? 0.0 : threshold;
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
        void tick_conflict() noexcept;

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
        static std::uint64_t rank_key(const clause::database::quality& quality) noexcept;

        static bool compare_subset_candidates(const clause::database& database, const std::uint64_t left_offset,
                                              const std::uint64_t right_offset) noexcept;

        static bool compare_candidates(const clause::database& database, const std::uint64_t left_offset,
                                       const std::uint64_t right_offset) noexcept;

        void evaluate_candidate(const clause::database& database, const clause::ref_t candidate) noexcept;

        bool retained_by_activity(const clause::database::quality& quality) const noexcept
        {
            return (quality.glue <= 2u) && (quality.activity >= activity_retention_threshold_) && (activity_retention_threshold_ > 0.0);
        }

        void trim_to_reduction_quota() noexcept;

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

    template <std::size_t Size>
    void reduce::select_reduction_candidates(const clause::database& database, const std::array<clause::ref_t, Size>& candidates) noexcept
    {
        ++select_call_count_;
        last_candidates_.clear();

        for (const auto candidate: candidates)
        {
            if (!candidate.valid() || database.is_reason_clause(candidate))
                continue;

            const auto tier = database.tier_of(candidate);
            const auto quality = database.quality_of(candidate);
            if ((tier >= clause::database::default_tier) && !retained_by_activity(quality))
                last_candidates_.push_back(candidate.offset());
        }

        std::sort(last_candidates_.begin(), last_candidates_.end(),
                  [&database](const auto left, const auto right) noexcept { return compare_subset_candidates(database, left, right); });

        trim_to_reduction_quota();
        last_selected_candidate_count_ = last_candidates_.size();
        reduced_candidates_ = last_selected_candidate_count_;
    }
}
