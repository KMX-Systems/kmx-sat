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
    ///
    /// This type exists only to document a deliberate exclusion: the plan considered a coroutine-based FSM
    /// (`co_fsm`) framework for orchestration and concluded it does not provide enough architectural value relative
    /// to its conceptual cost, and explicitly forbids the CDCL core from depending on asynchronous orchestration or
    /// coroutine-based FSM frameworks. This placeholder marks that decision in the namespace layout so a future
    /// contributor does not need to rediscover the reasoning before reintroducing it.
    /// @note No interface in the baseline plan; this is an empty placeholder marking the exclusion decision.
    class co_fsm_adapter final
    {
    public:
        /// @brief Constructs an empty placeholder instance.
        /// @throws None (noexcept).
        co_fsm_adapter() noexcept = default;

        /// @brief Activates the placeholder adapter.
        /// @throws None (noexcept).
        void activate() noexcept
        {
            active_ = true;
        }

        /// @brief Deactivates the placeholder adapter.
        /// @throws None (noexcept).
        void deactivate() noexcept
        {
            active_ = false;
        }

        /// @brief Returns whether the placeholder adapter is active.
        [[nodiscard]] bool active() const noexcept
        {
            return active_;
        }

    private:
        bool active_ {false};
    };
}
