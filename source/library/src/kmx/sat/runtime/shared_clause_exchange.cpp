/// @file library/src/kmx/sat/runtime/shared_clause_exchange.cpp
/// @brief Out-of-line definitions declared by kmx/sat/runtime/shared_clause_exchange.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/runtime/shared_clause_exchange.hpp>

namespace kmx::sat::runtime
{
    void shared_clause_exchange::publish_clause(const cdcl::clause::ref_t ref) noexcept
    {
        ++publish_attempt_count_;
        if (!ref.valid())
        {
            ++dropped_invalid_ref_count_;
            return;
        }
        if (std::find(pending_refs_.begin(), pending_refs_.end(), ref) != pending_refs_.end())
        {
            ++dropped_duplicate_ref_count_;
            return;
        }
        pending_refs_.push_back(ref);
    }

    void shared_clause_exchange::drain_incoming() noexcept
    {
        if (!pending_refs_.empty())
        {
            ++drain_count_;
            last_drain_size_ = pending_refs_.size();
            total_drained_refs_ += pending_refs_.size();
            pending_refs_.clear();
        }
    }

    void shared_clause_exchange::apply_exchange_policy() noexcept
    {
        policy_applied_ = true;
        ++policy_apply_count_;
        if (pending_refs_.size() > max_pending_)
        {
            truncated_by_policy_count_ += pending_refs_.size() - max_pending_;
            pending_refs_.resize(max_pending_);
        }
    }

    void shared_clause_exchange::set_max_pending(const std::size_t max_pending) noexcept
    {
        max_pending_ = max_pending;
        if (max_pending_ < 1u)
            max_pending_ = 1u;
        if (max_pending_ > 4096u)
            max_pending_ = 4096u;
        pending_refs_.reserve(max_pending_);
    }

    void shared_clause_exchange::reset_exchange_state() noexcept
    {
        pending_refs_.clear();
        drain_count_ = 0u;
        policy_applied_ = false;
        publish_attempt_count_ = 0u;
        dropped_invalid_ref_count_ = 0u;
        dropped_duplicate_ref_count_ = 0u;
        truncated_by_policy_count_ = 0u;
        policy_apply_count_ = 0u;
        last_drain_size_ = 0u;
        total_drained_refs_ = 0u;
    }

    [[nodiscard]] shared_clause_exchange::exchange_metrics shared_clause_exchange::exchange_metrics_snapshot() const noexcept
    {
        return exchange_metrics {
            .has_pending = has_pending(),
            .policy_applied = policy_applied_,
            .pending_count = pending_refs_.size(),
            .max_pending = max_pending_,
            .drain_count = drain_count_,
            .publish_attempt_count = publish_attempt_count_,
            .dropped_invalid_ref_count = dropped_invalid_ref_count_,
            .dropped_duplicate_ref_count = dropped_duplicate_ref_count_,
            .truncated_by_policy_count = truncated_by_policy_count_,
            .policy_apply_count = policy_apply_count_,
            .last_drain_size = last_drain_size_,
            .total_drained_refs = total_drained_refs_,
        };
    }

    bool shared_clause_exchange::exchange_metrics_monotonic(const exchange_metrics& before, const exchange_metrics& after) noexcept
    {
        return (after.drain_count >= before.drain_count) && (after.publish_attempt_count >= before.publish_attempt_count) &&
               (after.dropped_invalid_ref_count >= before.dropped_invalid_ref_count) &&
               (after.dropped_duplicate_ref_count >= before.dropped_duplicate_ref_count) &&
               (after.truncated_by_policy_count >= before.truncated_by_policy_count) &&
               (after.policy_apply_count >= before.policy_apply_count) && (after.total_drained_refs >= before.total_drained_refs);
    }
}
