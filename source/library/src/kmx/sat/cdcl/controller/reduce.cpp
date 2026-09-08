/// @file library/src/kmx/sat/cdcl/controller/reduce.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/controller/reduce.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/controller/reduce.hpp>

namespace kmx::sat::cdcl::controller
{
    void reduce::reset() noexcept
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

    void reduce::select_reduction_candidates() noexcept
    {
        ++select_call_count_;
        last_selected_candidate_count_ = 0u;
        reduced_candidates_ = 0u;
        last_candidates_.clear();
    }

    void reduce::select_reduction_candidates(const clause::database& database) noexcept
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
                if ((quality.tier >= clause::database::default_tier) && !retained_by_activity(quality) && (quality.used_count == 0u))
                    ranked_scratch_.push_back(ranked_candidate {rank_key(quality), candidate.offset()});
            });

        std::sort(ranked_scratch_.begin(), ranked_scratch_.end(), [](const ranked_candidate& left, const ranked_candidate& right) noexcept
                  { return (left.key != right.key) ? left.key > right.key : left.offset < right.offset; });
        last_candidates_.reserve(ranked_scratch_.size());
        for (const auto& ranked: ranked_scratch_)
            last_candidates_.push_back(ranked.offset);

        last_selected_candidate_count_ = last_candidates_.size();
        trim_to_reduction_quota();
        last_selected_candidate_count_ = last_candidates_.size();
        reduced_candidates_ = last_selected_candidate_count_;
    }

    void reduce::reduce_clauses(clause::database& database) noexcept
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

    void reduce::flush_redundant(clause::database& database) noexcept
    {
        ++flush_call_count_;
        const auto before = database.stats_snapshot().redundant_count;
        database.flush_satisfied([&](const clause::ref_t ref) noexcept { return database.is_garbage(ref); });
        const auto after = database.stats_snapshot().redundant_count;
        flushed_candidates_ = (before >= after) ? (before - after) : 0u;
    }

    void reduce::update_tiers() noexcept
    {
        ++update_tiers_call_count_;
        if (reduce_pending_)
            ++reduction_pass_count_;
        reduce_pending_ = false;
    }

    void reduce::update_tiers(clause::database& database) noexcept
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
            const auto tier = (quality.glue <= core_glue_limit_)     ? clause::database::tier_t {0u} :
                              (quality.glue <= retained_glue_limit_) ? clause::database::default_tier :
                                                                       clause::database::lowest_tier;
            database.set_tier(ref, tier);
            // A use counts two (`bump_clause`); the pass leaves one behind as a pass of grace for a mid-glue
            // clause and clears it for a high-glue one, so a clause not used again becomes a candidate after
            // one pass (high glue) or two (mid glue) instead of staying protected forever.
            const auto grace =
                ((quality.used_count >= 2u) && (tier == clause::database::default_tier)) ? std::uint8_t {1u} : std::uint8_t {0u};
            database.set_used_count(ref, grace);
        }
    }

    void reduce::tick_conflict() noexcept
    {
        ++conflict_count_;
        if ((reduction_interval_ != 0u) && (conflict_count_ >= next_reduction_at_))
        {
            reduce_pending_ = true;
            next_reduction_at_ = conflict_count_ + reduction_interval_;
        }
    }

    std::uint64_t reduce::rank_key(const clause::database::quality& quality) noexcept
    {
        const auto tier = static_cast<std::uint64_t>(std::min<std::uint32_t>(quality.tier, 3u));
        const auto glue = static_cast<std::uint64_t>(std::min<std::uint32_t>(quality.glue, 4095u));
        const auto unused = static_cast<std::uint64_t>(255u - std::min<std::uint32_t>(quality.used_count, 255u));
        const auto activity = (quality.activity <= 0.0) ? 0.0 : std::min(quality.activity, 65535.0);
        const auto inactivity = static_cast<std::uint64_t>(65535u - static_cast<std::uint32_t>(activity));
        const auto size = static_cast<std::uint64_t>(std::min<std::uint32_t>(quality.size, 65535u));
        return (tier << 60u) | (glue << 48u) | (unused << 40u) | (inactivity << 24u) | (size << 8u);
    }

    bool reduce::compare_subset_candidates(const clause::database& database, const std::uint64_t left_offset,
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

    bool reduce::compare_candidates(const clause::database& database, const std::uint64_t left_offset,
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

    void reduce::evaluate_candidate(const clause::database& database, const clause::ref_t candidate) noexcept
    {
        if (!candidate.valid() || database.is_reason_clause(candidate))
            return;

        const auto tier = database.tier_of(candidate);
        const auto quality = database.quality_of(candidate);
        if ((tier >= clause::database::default_tier) && !retained_by_activity(quality))
            last_candidates_.push_back(candidate.offset());
    }

    void reduce::trim_to_reduction_quota() noexcept
    {
        if (last_candidates_.empty() || (reduction_fraction_percent_ >= 100u))
            return;

        const auto quota = std::max<std::size_t>(1u, (last_candidates_.size() * reduction_fraction_percent_ + 99u) / 100u);
        if (quota < last_candidates_.size())
            last_candidates_.resize(quota);
    }
}
