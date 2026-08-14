/// @file inc/kmx/sat/runtime/co_fsm_adapter.hpp
/// @brief Excluded from the architectural core; may exist only as a separate control-plane experiment.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::runtime
{
    /// @brief Excluded from the architectural core; may exist only as a separate control-plane experiment.
    /// @details
    /// This type exists only to document a deliberate exclusion: the plan considered a coroutine-based FSM
    /// (`co_fsm`) framework for orchestration and concluded it does not provide enough architectural value relative
    /// to its conceptual cost, and explicitly forbids the CDCL core from depending on asynchronous orchestration or
    /// coroutine-based FSM frameworks. This placeholder marks that decision in the namespace layout so a future
    /// contributor does not need to rediscover the reasoning before reintroducing it.
    /// @note No interface in the baseline plan; this is an empty placeholder marking the exclusion decision.
    class co_fsm_adapter final
    {
    public:
        struct lifecycle_metrics final
        {
            bool active {};
            std::uint32_t activation_count {};
            std::uint32_t deactivation_count {};
            std::uint32_t transition_epoch {};
            std::uint32_t last_activation_epoch {};
        };

        /// @brief Constructs an empty placeholder instance.
        /// @throws None (noexcept).
        co_fsm_adapter() noexcept = default;

        /// @brief Activates the placeholder adapter.
        /// @throws None (noexcept).
        void activate() noexcept
        {
            if (!active_)
            {
                active_ = true;
                ++activation_count_;
                ++transition_epoch_;
                last_activation_epoch_ = transition_epoch_;
            }
        }

        /// @brief Deactivates the placeholder adapter.
        /// @throws None (noexcept).
        void deactivate() noexcept
        {
            if (active_)
            {
                active_ = false;
                ++deactivation_count_;
                ++transition_epoch_;
            }
        }

        /// @brief Returns whether the placeholder adapter is active.
        [[nodiscard]] bool active() const noexcept { return active_; }

        [[nodiscard]] std::uint32_t activation_count() const noexcept { return activation_count_; }

        [[nodiscard]] std::uint32_t deactivation_count() const noexcept { return deactivation_count_; }

        [[nodiscard]] std::uint32_t transition_epoch() const noexcept { return transition_epoch_; }

        [[nodiscard]] std::uint32_t last_activation_epoch() const noexcept { return last_activation_epoch_; }

        [[nodiscard]] lifecycle_metrics lifecycle_metrics_snapshot() const noexcept
        {
            return lifecycle_metrics {
                .active = active_,
                .activation_count = activation_count_,
                .deactivation_count = deactivation_count_,
                .transition_epoch = transition_epoch_,
                .last_activation_epoch = last_activation_epoch_,
            };
        }

        void reset_lifecycle_metrics() noexcept
        {
            active_ = false;
            activation_count_ = 0u;
            deactivation_count_ = 0u;
            transition_epoch_ = 0u;
            last_activation_epoch_ = 0u;
        }

        static bool lifecycle_monotonic(const lifecycle_metrics& before, const lifecycle_metrics& after) noexcept
        {
            return after.activation_count >= before.activation_count && after.deactivation_count >= before.deactivation_count &&
                   after.transition_epoch >= before.transition_epoch && after.last_activation_epoch >= before.last_activation_epoch;
        }

    private:
        bool active_ {};
        std::uint32_t activation_count_ {};
        std::uint32_t deactivation_count_ {};
        std::uint32_t transition_epoch_ {};
        std::uint32_t last_activation_epoch_ {};
    };
}
