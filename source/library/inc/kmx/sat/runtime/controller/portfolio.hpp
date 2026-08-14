/// @file inc/kmx/sat/runtime/controller/portfolio.hpp
/// @brief Advanced option, kept strictly separate from the single-engine core.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/runtime/controller/signal.hpp>

namespace kmx::sat::runtime::controller
{
    /// @brief Advanced option, kept strictly separate from the single-engine core.
    /// @details
    /// `controller::portfolio` runs several independent `solver_core` instances concurrently, each with a distinct
    /// `random_engine` seed, racing them against the same input to reduce wall-clock time on hard instances at the
    /// cost of the single-engine determinism guarantee (unless a fixed-schedule deterministic portfolio mode is
    /// separately enabled). `launch_strategies` starts the configured instances; `cancel_others_on_result` stops the
    /// remaining instances once one reports a definite SAT/UNSAT result, using the embedded `controller::signal` to
    /// request their orderly termination; `collect_winner` retrieves the winning instance's result for the caller.
    /// If clause sharing across instances is enabled, it must go through `shared_clause_exchange` rather than any
    /// direct coupling between instances.
    /// @note This controller is entirely optional and feature-gated; the CDCL core has no dependency on it, and its
    /// absence must not alter baseline single-engine solver behavior.
    class portfolio final
    {
    public:
        struct lifecycle_metrics final
        {
            bool launched {};
            bool cancelled {};
            bool collected {};
            bool termination_requested {};
            std::uint32_t launch_count {};
            std::uint32_t launch_epoch {};
            std::uint32_t cancel_count {};
            std::uint32_t collect_count {};
            std::uint32_t strategy_budget {};
        };

        /// @brief Constructs a portfolio controller with an embedded signal controller.
        /// @throws None (noexcept).
        portfolio() noexcept = default;

        void set_strategy_budget(std::uint32_t strategy_budget) noexcept
        {
            strategy_budget_ = strategy_budget;
            if (strategy_budget_ < 1u)
                strategy_budget_ = 1u;
            if (strategy_budget_ > 64u)
                strategy_budget_ = 64u;
        }

        /// @brief Launches the configured set of independently seeded solver instances.
        /// @throws None (noexcept).
        void launch_strategies() noexcept
        {
            launched_ = true;
            cancelled_ = false;
            collected_ = false;
            signal_.clear();
            launch_count_ += 1u;
            ++launch_epoch_;
        }

        /// @brief Requests orderly termination of every instance other than the one that produced a result.
        /// @throws None (noexcept).
        void cancel_others_on_result() noexcept
        {
            if (launched_ && !cancelled_)
            {
                cancelled_ = true;
                signal_.request_stop();
                ++cancel_count_;
            }
        }

        /// @brief Retrieves the result from whichever instance finished first.
        /// @throws None (noexcept).
        void collect_winner() noexcept
        {
            if (launched_ && !collected_)
            {
                collected_ = true;
                ++collect_count_;
            }
        }

        [[nodiscard]] bool launched() const noexcept { return launched_; }

        [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }

        [[nodiscard]] bool collected() const noexcept { return collected_; }

        [[nodiscard]] std::uint32_t launch_count() const noexcept { return launch_count_; }

        [[nodiscard]] std::uint32_t launch_epoch() const noexcept { return launch_epoch_; }

        [[nodiscard]] std::uint32_t cancel_count() const noexcept { return cancel_count_; }

        [[nodiscard]] std::uint32_t collect_count() const noexcept { return collect_count_; }

        [[nodiscard]] std::uint32_t strategy_budget() const noexcept { return strategy_budget_; }

        [[nodiscard]] bool termination_requested() const noexcept { return signal_.termination_requested(); }

        [[nodiscard]] lifecycle_metrics lifecycle_metrics_snapshot() const noexcept
        {
            return lifecycle_metrics {
                .launched = launched_,
                .cancelled = cancelled_,
                .collected = collected_,
                .termination_requested = signal_.termination_requested(),
                .launch_count = launch_count_,
                .launch_epoch = launch_epoch_,
                .cancel_count = cancel_count_,
                .collect_count = collect_count_,
                .strategy_budget = strategy_budget_,
            };
        }

        void reset_lifecycle_metrics() noexcept
        {
            signal_.reset_metrics();
            launched_ = false;
            cancelled_ = false;
            collected_ = false;
            launch_count_ = 0u;
            launch_epoch_ = 0u;
            cancel_count_ = 0u;
            collect_count_ = 0u;
            strategy_budget_ = 1u;
        }

        static bool lifecycle_monotonic(const lifecycle_metrics& before, const lifecycle_metrics& after) noexcept
        {
            return after.launch_count >= before.launch_count && after.launch_epoch >= before.launch_epoch &&
                   after.cancel_count >= before.cancel_count && after.collect_count >= before.collect_count;
        }

    private:
        signal signal_ {};
        bool launched_ {};
        bool cancelled_ {};
        bool collected_ {};
        std::uint32_t launch_count_ {};
        std::uint32_t launch_epoch_ {};
        std::uint32_t cancel_count_ {};
        std::uint32_t collect_count_ {};
        std::uint32_t strategy_budget_ {1u};
    };
}
