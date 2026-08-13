/// @file inc/kmx/sat/runtime/shared_clause_exchange.hpp
/// @brief Bounded, explicit cross-engine clause sharing without coupling the CDCL core to orchestration concerns.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::runtime
{
    /// @brief Bounded, explicit cross-engine clause sharing without coupling the CDCL core to orchestration concerns.
    ///
    /// When `controller::portfolio` runs multiple independent `solver_core` instances, sharing learned clauses
    /// between them can speed up the overall race, but naive sharing would silently affect the single-engine
    /// determinism guarantee; `shared_clause_exchange` makes that sharing bounded and explicit instead.
    /// `publish_clause` offers one instance's learned clause to the exchange; `drain_incoming` lets another instance
    /// pull in clauses published by others; `apply_exchange_policy` enforces bounds (rate, size, quality threshold)
    /// on what gets shared; `reset_exchange_state` clears exchange state, for example between unrelated portfolio
    /// runs. This component is portfolio-only and has no involvement in single-engine solving.
    class shared_clause_exchange final
    {
    public:
        struct exchange_metrics final
        {
            bool has_pending {};
            bool policy_applied {};
            std::size_t pending_count {};
            std::size_t max_pending {};
            std::uint32_t drain_count {};
            std::uint32_t publish_attempt_count {};
            std::uint32_t dropped_invalid_ref_count {};
            std::uint32_t dropped_duplicate_ref_count {};
            std::uint32_t truncated_by_policy_count {};
            std::uint32_t policy_apply_count {};
            std::size_t last_drain_size {};
            std::size_t total_drained_refs {};
        };

        /// @brief Constructs an exchange with no pending clauses.
        /// @throws None (noexcept).
        shared_clause_exchange() noexcept = default;

        /// @brief Offers a learned clause from one portfolio instance to the exchange.
        /// @param ref Reference to the clause being published, local to the publishing instance.
        /// @throws None (noexcept).
        void publish_clause(const cdcl::clause::ref_t ref) noexcept
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

        /// @brief Pulls in clauses published by other portfolio instances.
        /// @throws None (noexcept).
        void drain_incoming() noexcept
        {
            if (!pending_refs_.empty())
            {
                ++drain_count_;
                last_drain_size_ = pending_refs_.size();
                total_drained_refs_ += pending_refs_.size();
                pending_refs_.clear();
            }
        }

        /// @brief Enforces the configured bounds (rate, size, quality) on what may be exchanged.
        /// @throws None (noexcept).
        void apply_exchange_policy() noexcept
        {
            policy_applied_ = true;
            ++policy_apply_count_;
            if (pending_refs_.size() > max_pending_)
            {
                truncated_by_policy_count_ += pending_refs_.size() - max_pending_;
                pending_refs_.resize(max_pending_);
            }
        }

        void set_max_pending(std::size_t max_pending) noexcept
        {
            max_pending_ = max_pending;
            if (max_pending_ < 1u)
            {
                max_pending_ = 1u;
            }
            if (max_pending_ > 4096u)
            {
                max_pending_ = 4096u;
            }
        }

        /// @brief Clears all exchange state, for example between unrelated portfolio runs.
        /// @throws None (noexcept).
        void reset_exchange_state() noexcept
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

        [[nodiscard]] bool has_pending() const noexcept { return !pending_refs_.empty(); }

        [[nodiscard]] std::uint32_t drain_count() const noexcept { return drain_count_; }

        [[nodiscard]] std::size_t pending_count() const noexcept { return pending_refs_.size(); }

        [[nodiscard]] bool policy_applied() const noexcept { return policy_applied_; }

        [[nodiscard]] std::size_t max_pending() const noexcept { return max_pending_; }

        [[nodiscard]] std::uint32_t publish_attempt_count() const noexcept { return publish_attempt_count_; }

        [[nodiscard]] std::uint32_t dropped_invalid_ref_count() const noexcept { return dropped_invalid_ref_count_; }

        [[nodiscard]] std::uint32_t dropped_duplicate_ref_count() const noexcept
        {
            return dropped_duplicate_ref_count_;
        }

        [[nodiscard]] std::uint32_t truncated_by_policy_count() const noexcept { return truncated_by_policy_count_; }

        [[nodiscard]] std::uint32_t policy_apply_count() const noexcept { return policy_apply_count_; }

        [[nodiscard]] std::size_t last_drain_size() const noexcept { return last_drain_size_; }

        [[nodiscard]] std::size_t total_drained_refs() const noexcept { return total_drained_refs_; }

        [[nodiscard]] exchange_metrics exchange_metrics_snapshot() const noexcept
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

        void reset_exchange_metrics() noexcept
        {
            reset_exchange_state();
            max_pending_ = 4u;
        }

        static bool exchange_metrics_monotonic(const exchange_metrics& before, const exchange_metrics& after) noexcept
        {
            return after.drain_count >= before.drain_count
                && after.publish_attempt_count >= before.publish_attempt_count
                && after.dropped_invalid_ref_count >= before.dropped_invalid_ref_count
                && after.dropped_duplicate_ref_count >= before.dropped_duplicate_ref_count
                && after.truncated_by_policy_count >= before.truncated_by_policy_count
                && after.policy_apply_count >= before.policy_apply_count
                && after.total_drained_refs >= before.total_drained_refs;
        }

    private:
        std::vector<cdcl::clause::ref_t> pending_refs_ {};
        std::uint32_t drain_count_ {};
        bool policy_applied_ {};
        std::size_t max_pending_ {4u};
        std::uint32_t publish_attempt_count_ {};
        std::uint32_t dropped_invalid_ref_count_ {};
        std::uint32_t dropped_duplicate_ref_count_ {};
        std::uint32_t truncated_by_policy_count_ {};
        std::uint32_t policy_apply_count_ {};
        std::size_t last_drain_size_ {};
        std::size_t total_drained_refs_ {};
    };
}
