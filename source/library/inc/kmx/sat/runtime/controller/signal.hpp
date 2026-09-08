/// @file inc/kmx/sat/runtime/controller/signal.hpp
/// @brief Orderly response to SIGINT/SIGTERM.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <csignal>
    #include <cstdint>
#endif

namespace kmx::sat::runtime::controller
{
    /// @brief Orderly response to SIGINT/SIGTERM.
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
        struct metrics final
        {
            bool handlers_installed {};
            bool termination_requested {};
            std::uint32_t install_count {};
            std::uint32_t stop_request_count {};
            std::uint32_t os_signal_request_count {};
            std::uint32_t clear_count {};
        };

        /// @brief Constructs a signal controller with no installed handlers.
        /// @throws None (noexcept).
        signal() noexcept = default;

        /// @brief Installs async-signal-safe OS handlers for SIGINT/SIGTERM.
        /// @throws None (noexcept).
        void install_handlers() noexcept;

        /// @brief Checks whether a termination request is currently pending.
        /// @return True if termination has been requested and not yet cleared.
        /// @throws None (noexcept).
        bool termination_requested() const noexcept { return termination_requested_ || (pending_signal_ != 0); }

        /// @brief Requests an orderly stop programmatically, without going through an OS signal.
        /// @throws None (noexcept).
        void request_stop() noexcept
        {
            termination_requested_ = true;
            ++stop_request_count_;
        }

        /// @brief Requests an orderly stop through the installed OS-signal path.
        /// @throws None (noexcept).
        void notify_os_signal() noexcept;

        /// @brief Clears any pending termination request.
        /// @throws None (noexcept).
        void clear() noexcept;

        [[nodiscard]] bool handlers_installed() const noexcept { return handlers_installed_; }

        [[nodiscard]] std::uint32_t install_count() const noexcept { return install_count_; }

        [[nodiscard]] std::uint32_t stop_request_count() const noexcept { return stop_request_count_; }

        [[nodiscard]] std::uint32_t os_signal_request_count() const noexcept { return os_signal_request_count_; }

        [[nodiscard]] std::uint32_t clear_count() const noexcept { return clear_count_; }

        [[nodiscard]] metrics metrics_snapshot() const noexcept;

        void reset_metrics() noexcept;

        static bool metrics_monotonic(const metrics& before, const metrics& after) noexcept
        {
            return (after.install_count >= before.install_count) && (after.stop_request_count >= before.stop_request_count) &&
                   (after.os_signal_request_count >= before.os_signal_request_count) && (after.clear_count >= before.clear_count);
        }

    private:
        static void handle_signal(const int) noexcept { pending_signal_ = 1; }

        inline static volatile std::sig_atomic_t pending_signal_ {};
        bool handlers_installed_ {};
        bool termination_requested_ {};
        std::uint32_t install_count_ {};
        std::uint32_t stop_request_count_ {};
        std::uint32_t os_signal_request_count_ {};
        std::uint32_t clear_count_ {};
    };
}
