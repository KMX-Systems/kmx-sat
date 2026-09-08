/// @file library/src/kmx/sat/runtime/controller/portfolio.cpp
/// @brief Out-of-line definitions declared by kmx/sat/runtime/controller/portfolio.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/runtime/controller/portfolio.hpp>

namespace kmx::sat::runtime::controller
{
    void portfolio::set_strategy_budget(std::uint32_t strategy_budget) noexcept
    {
        strategy_budget_ = strategy_budget;
        if (strategy_budget_ < 1u)
            strategy_budget_ = 1u;
        if (strategy_budget_ > 64u)
            strategy_budget_ = 64u;
    }

    void portfolio::launch_strategies() noexcept
    {
        launched_ = true;
        cancelled_ = false;
        collected_ = false;
        signal_.clear();
        launch_count_ += 1u;
        ++launch_epoch_;
    }

    void portfolio::cancel_others_on_result() noexcept
    {
        if (launched_ && !cancelled_)
        {
            cancelled_ = true;
            signal_.request_stop();
            ++cancel_count_;
        }
    }

    void portfolio::collect_winner() noexcept
    {
        if (launched_ && !collected_)
        {
            collected_ = true;
            ++collect_count_;
        }
    }

    [[nodiscard]] portfolio::lifecycle_metrics portfolio::lifecycle_metrics_snapshot() const noexcept
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

    void portfolio::reset_lifecycle_metrics() noexcept
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
}
