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
    /// @details
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
        void publish_clause(const cdcl::clause::ref_t ref) noexcept;

        /// @brief Pulls in clauses published by other portfolio instances.
        /// @throws None (noexcept).
        void drain_incoming() noexcept;

        /// @brief Enforces the configured bounds (rate, size, quality) on what may be exchanged.
        /// @throws None (noexcept).
        void apply_exchange_policy() noexcept;

        void set_max_pending(const std::size_t max_pending) noexcept;

        /// @brief Clears all exchange state, for example between unrelated portfolio runs.
        /// @throws None (noexcept).
        void reset_exchange_state() noexcept;

        [[nodiscard]] bool has_pending() const noexcept { return !pending_refs_.empty(); }

        [[nodiscard]] std::uint32_t drain_count() const noexcept { return drain_count_; }

        [[nodiscard]] std::size_t pending_count() const noexcept { return pending_refs_.size(); }

        [[nodiscard]] bool policy_applied() const noexcept { return policy_applied_; }

        [[nodiscard]] std::size_t max_pending() const noexcept { return max_pending_; }

        [[nodiscard]] std::uint32_t publish_attempt_count() const noexcept { return publish_attempt_count_; }

        [[nodiscard]] std::uint32_t dropped_invalid_ref_count() const noexcept { return dropped_invalid_ref_count_; }

        [[nodiscard]] std::uint32_t dropped_duplicate_ref_count() const noexcept { return dropped_duplicate_ref_count_; }

        [[nodiscard]] std::uint32_t truncated_by_policy_count() const noexcept { return truncated_by_policy_count_; }

        [[nodiscard]] std::uint32_t policy_apply_count() const noexcept { return policy_apply_count_; }

        [[nodiscard]] std::size_t last_drain_size() const noexcept { return last_drain_size_; }

        [[nodiscard]] std::size_t total_drained_refs() const noexcept { return total_drained_refs_; }

        [[nodiscard]] exchange_metrics exchange_metrics_snapshot() const noexcept;

        void reset_exchange_metrics() noexcept
        {
            reset_exchange_state();
            max_pending_ = 4u;
        }

        static bool exchange_metrics_monotonic(const exchange_metrics& before, const exchange_metrics& after) noexcept;

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
