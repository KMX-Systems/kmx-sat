/// @file inc/kmx/sat/telemetry/report_formatter.hpp
/// @brief Reporting output and benchmark harness compatibility.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <sstream>
    #include <string>
#endif
#include <kmx/sat/telemetry/profile_clock.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>

namespace kmx::sat::telemetry
{
    /// @brief Reporting output and benchmark harness compatibility.
    ///
    /// @details
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
            std::ostringstream stream;
            append_statistics(stream, snapshot);
            return stream.str();
        }

        /// @brief Renders a detailed statistics line including callback/configuration activity counters.
        /// @param snapshot Statistics snapshot to render.
        /// @return Formatted verbose statistics line.
        /// @throws None (noexcept).
        std::string format_statistics_line_verbose(const solver_statistics::snapshot& snapshot) const noexcept
        {
            std::ostringstream stream;
            append_statistics(stream, snapshot);
            stream << " terminate_callback_calls=" << snapshot.terminate_callback_calls
                   << " learn_callback_calls=" << snapshot.learn_callback_calls
                   << " external_propagator_calls=" << snapshot.external_propagator_calls << " option_updates=" << snapshot.option_updates
                   << " configuration_updates=" << snapshot.configuration_updates;
            return stream.str();
        }

        /// @brief Renders current resource usage (time, memory) as one formatted reporting line.
        /// @return Formatted resource-usage line.
        /// @throws None (noexcept).
        std::string format_resource_line() const noexcept
        {
            std::ostringstream stream;
            stream << "time=" << profile_clock_.current_wall_time() << "ms cpu=" << profile_clock_.current_process_time() << "ms";
            return stream.str();
        }

        /// @brief Renders a short progress indicator for interactive/CI consumption.
        /// @return Formatted progress string.
        /// @throws None (noexcept).
        std::string format_progress() const noexcept
        {
            ++progress_steps_;
            return "progress: step " + std::to_string(progress_steps_) + " of " + std::to_string(total_steps_);
        }

        std::size_t progress_steps() const noexcept { return progress_steps_; }

    private:
        static void append_statistics(std::ostringstream& stream, const solver_statistics::snapshot& snapshot) noexcept
        {
            stream << "conflicts=" << snapshot.conflicts << " decisions=" << snapshot.decisions << " propagations=" << snapshot.propagations
                   << " restarts=" << snapshot.restarts << " learned_clauses=" << snapshot.learned_clauses
                   << " learned_clause_glue_total=" << snapshot.learned_clause_glue_total
                   << " learned_clause_glue_samples=" << snapshot.learned_clause_glue_samples
                   << " reduction_passes=" << snapshot.reduction_passes << " reduced_clauses=" << snapshot.reduced_clauses
                   << " deleted_clauses=" << snapshot.deleted_clauses;
        }

        mutable profile_clock profile_clock_ {};
        mutable std::size_t progress_steps_ {};
        mutable std::size_t total_steps_ {10u};
    };
}
