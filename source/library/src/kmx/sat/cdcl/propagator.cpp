/// @file library/src/kmx/sat/cdcl/propagator.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/propagator.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/propagator.hpp>

namespace kmx::sat::cdcl
{
    clause::ref_t propagator::propagate() noexcept
    {
        ++propagation_call_count_;
        if (has_staged_conflicts())
            return consume_staged_conflict();
        return {};
    }

    clause::ref_t propagator::propagate_assumptions() noexcept
    {
        ++assumption_propagation_call_count_;

        if (pending_assumption_count_ == 0u)
        {
            // No assumptions are pending for this episode: any currently staged conflict was not caused by
            // assumption processing (e.g. it was staged directly ahead of the regular search loop), so it
            // must not be misattributed here; leave it for `propagate` to discover in the normal loop.
            return {};
        }

        pending_assumption_count_ = 0u;
        if (has_staged_conflicts())
            return consume_staged_conflict();
        return {};
    }

    clause::ref_t propagator::propagate_beyond_conflict() noexcept
    {
        ++beyond_conflict_propagation_call_count_;
        if (has_staged_conflicts())
            return consume_staged_conflict();
        return {};
    }

    void propagator::watch_clause(const clause::ref_t ref) noexcept
    {
        if (!ref.valid() || is_watched(ref))
            return;
        watched_.push_back(ref);
    }

    void propagator::detach_clause(const clause::ref_t ref) noexcept
    {
        const auto it = std::find(watched_.begin(), watched_.end(), ref);
        if (it != watched_.end())
        {
            std::swap(*it, watched_.back());
            watched_.pop_back();
        }
    }

    void propagator::stage_conflict(const clause::ref_t ref) noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return;
        staged_conflicts_.push_back(ref);
    }

    void propagator::reset_episode_state() noexcept
    {
        staged_conflicts_.clear();
        staged_conflict_head_ = 0u;
        pending_assumption_count_ = 0u;
    }

    clause::ref_t propagator::consume_staged_conflict() noexcept
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
}
