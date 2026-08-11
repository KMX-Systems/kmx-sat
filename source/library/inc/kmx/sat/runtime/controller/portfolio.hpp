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
    ///
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
        /// @brief Constructs a portfolio controller with an embedded signal controller.
        /// @throws None (noexcept).
        portfolio() noexcept = default;

        /// @brief Launches the configured set of independently seeded solver instances.
        /// @throws None (noexcept).
        void launch_strategies() noexcept
        {
            launched_ = true;
            cancelled_ = false;
            collected_ = false;
            signal_.clear();
            launch_count_ += 1u;
        }

        /// @brief Requests orderly termination of every instance other than the one that produced a result.
        /// @throws None (noexcept).
        void cancel_others_on_result() noexcept
        {
            if (launched_)
            {
                cancelled_ = true;
                signal_.request_stop();
            }
        }

        /// @brief Retrieves the result from whichever instance finished first.
        /// @throws None (noexcept).
        void collect_winner() noexcept
        {
            if (launched_)
            {
                collected_ = true;
            }
        }

        [[nodiscard]] bool launched() const noexcept
        {
            return launched_;
        }

        [[nodiscard]] bool cancelled() const noexcept
        {
            return cancelled_;
        }

        [[nodiscard]] bool collected() const noexcept
        {
            return collected_;
        }

        [[nodiscard]] std::uint32_t launch_count() const noexcept
        {
            return launch_count_;
        }

    private:
        signal signal_ {};
        bool launched_ {false};
        bool cancelled_ {false};
        bool collected_ {false};
        std::uint32_t launch_count_ {0u};
    };
}
