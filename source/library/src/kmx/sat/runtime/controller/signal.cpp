/// @file library/src/kmx/sat/runtime/controller/signal.cpp
/// @brief Out-of-line definitions declared by kmx/sat/runtime/controller/signal.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/runtime/controller/signal.hpp>

namespace kmx::sat::runtime::controller
{
    void signal::install_handlers() noexcept
    {
        if (!handlers_installed_)
        {
            std::signal(SIGINT, &signal::handle_signal);
            std::signal(SIGTERM, &signal::handle_signal);
            handlers_installed_ = true;
            ++install_count_;
        }
    }

    void signal::notify_os_signal() noexcept
    {
        if (!handlers_installed_)
            return;
        request_stop();
        ++os_signal_request_count_;
    }

    void signal::clear() noexcept
    {
        termination_requested_ = false;
        pending_signal_ = 0;
        ++clear_count_;
    }

    [[nodiscard]] signal::metrics signal::metrics_snapshot() const noexcept
    {
        return metrics {
            .handlers_installed = handlers_installed_,
            .termination_requested = termination_requested_,
            .install_count = install_count_,
            .stop_request_count = stop_request_count_,
            .os_signal_request_count = os_signal_request_count_,
            .clear_count = clear_count_,
        };
    }

    void signal::reset_metrics() noexcept
    {
        handlers_installed_ = false;
        termination_requested_ = false;
        pending_signal_ = 0;
        install_count_ = 0u;
        stop_request_count_ = 0u;
        os_signal_request_count_ = 0u;
        clear_count_ = 0u;
    }
}
