/// @file library/src/kmx/sat/runtime/co_fsm_adapter.cpp
/// @brief Out-of-line definitions declared by kmx/sat/runtime/co_fsm_adapter.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/runtime/co_fsm_adapter.hpp>

namespace kmx::sat::runtime
{
    void co_fsm_adapter::activate() noexcept
    {
        if (!active_)
        {
            active_ = true;
            ++activation_count_;
            ++transition_epoch_;
            last_activation_epoch_ = transition_epoch_;
        }
    }

    void co_fsm_adapter::deactivate() noexcept
    {
        if (active_)
        {
            active_ = false;
            ++deactivation_count_;
            ++transition_epoch_;
        }
    }

    [[nodiscard]] co_fsm_adapter::lifecycle_metrics co_fsm_adapter::lifecycle_metrics_snapshot() const noexcept
    {
        return lifecycle_metrics {
            .active = active_,
            .activation_count = activation_count_,
            .deactivation_count = deactivation_count_,
            .transition_epoch = transition_epoch_,
            .last_activation_epoch = last_activation_epoch_,
        };
    }

    void co_fsm_adapter::reset_lifecycle_metrics() noexcept
    {
        active_ = false;
        activation_count_ = 0u;
        deactivation_count_ = 0u;
        transition_epoch_ = 0u;
        last_activation_epoch_ = 0u;
    }
}
