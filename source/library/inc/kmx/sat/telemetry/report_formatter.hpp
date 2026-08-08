/// @file inc/kmx/sat/telemetry/report_formatter.hpp
/// @brief Reporting output and benchmark harness compatibility.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <string>
#endif
#include <kmx/sat/telemetry/profile_clock.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>

namespace kmx::sat::telemetry
{
    /// @brief Reporting output and benchmark harness compatibility.
    ///
    /// `report_formatter` renders `solver_statistics::snapshot`/`profile_clock`/`memory_governor` data into the
    /// human- and tool-readable text lines expected by SAT competition/benchmark tooling and interactive use:
    /// `format_statistics_line` renders one statistics snapshot as a single reporting line;
    /// `format_resource_line` renders current resource usage (time, memory) in the same style CaDiCaL/Kissat use for
    /// their periodic status lines; `format_progress` renders a short progress indicator for interactive/CI
    /// consumption. Output from this class feeds `io::writer::format::write_report_line` and the comparative
    /// benchmarking harness's ledger.
    class report_formatter final
    {
    public:
        /// @brief Constructs a report formatter with default line formatting.
        /// @throws None (noexcept).
        report_formatter() noexcept = default;

        /// @brief Renders a statistics snapshot as one formatted reporting line.
        /// @param snapshot Statistics snapshot to render.
        /// @return Formatted statistics line.
        /// @throws None (noexcept).
        std::string format_statistics_line(const solver_statistics::snapshot& snapshot) const noexcept
        {
            return {};
        }

        /// @brief Renders current resource usage (time, memory) as one formatted reporting line.
        /// @return Formatted resource-usage line.
        /// @throws None (noexcept).
        std::string format_resource_line() const noexcept
        {
            return {};
        }

        /// @brief Renders a short progress indicator for interactive/CI consumption.
        /// @return Formatted progress string.
        /// @throws None (noexcept).
        std::string format_progress() const noexcept
        {
            return {};
        }
    };
}
