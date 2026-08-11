/// @file inc/kmx/sat/runtime/controller/signal.hpp
/// @brief Orderly response to SIGINT/SIGTERM.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif

namespace kmx::sat::runtime::controller
{
    /// @brief Orderly response to SIGINT/SIGTERM.
    ///
    /// @details
    /// `controller::signal` lets the solver respond to an OS termination request (Ctrl-C or a competition
    /// wall-clock-limit `SIGTERM`) without corrupting state: `install_handlers` registers OS signal handlers that
    /// only set an internal flag (async-signal-safe), never touching solver state directly; `termination_requested`
    /// is polled by `search_coordinator::handle_termination` and `preprocess`/`inprocess` schedulers at safe points;
    /// `request_stop` allows a non-signal-based caller (for example a portfolio controller) to request the same
    /// orderly stop; `clear` resets the flag, for example after a caller has consumed the pending termination.
    /// @note This mirrors the architecture's orderly-termination risk point: on termination the solver must reach
    /// either a consistent proof or an explicitly interrupted proof state, never a half-written one; termination
    /// must be observed at a safe point, not from inside the async signal handler itself.
    class signal final
    {
    public:
        /// @brief Constructs a signal controller with no installed handlers.
        /// @throws None (noexcept).
        signal() noexcept = default;

        /// @brief Installs async-signal-safe OS handlers for SIGINT/SIGTERM.
        /// @throws None (noexcept).
        void install_handlers() noexcept
        {
            handlers_installed_ = true;
        }

        /// @brief Checks whether a termination request is currently pending.
        /// @return True if termination has been requested and not yet cleared.
        /// @throws None (noexcept).
        bool termination_requested() const noexcept
        {
            return termination_requested_;
        }

        /// @brief Requests an orderly stop programmatically, without going through an OS signal.
        /// @throws None (noexcept).
        void request_stop() noexcept
        {
            termination_requested_ = true;
        }

        /// @brief Clears any pending termination request.
        /// @throws None (noexcept).
        void clear() noexcept
        {
            termination_requested_ = false;
        }

    private:
        bool handlers_installed_ {false};
        bool termination_requested_ {false};
    };
}
