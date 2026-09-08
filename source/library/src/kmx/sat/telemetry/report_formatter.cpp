/// @file library/src/kmx/sat/telemetry/report_formatter.cpp
/// @brief Out-of-line definitions declared by kmx/sat/telemetry/report_formatter.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/telemetry/report_formatter.hpp>

namespace kmx::sat::telemetry
{
    std::string report_formatter::format_statistics_line(const solver_statistics::snapshot& snapshot) const noexcept
    {
        std::ostringstream stream;
        append_statistics(stream, snapshot);
        return stream.str();
    }

    std::string report_formatter::format_statistics_line_verbose(const solver_statistics::snapshot& snapshot) const noexcept
    {
        std::ostringstream stream;
        append_statistics(stream, snapshot);
        append_counter(stream, counter_id::terminate_callback_calls, snapshot.terminate_callback_calls);
        append_counter(stream, counter_id::learn_callback_calls, snapshot.learn_callback_calls);
        append_counter(stream, counter_id::external_propagator_calls, snapshot.external_propagator_calls);
        append_counter(stream, counter_id::option_updates, snapshot.option_updates);
        append_counter(stream, counter_id::configuration_updates, snapshot.configuration_updates);
        return stream.str();
    }

    std::string report_formatter::format_resource_line() const noexcept
    {
        std::ostringstream stream;
        stream << "time=" << profile_clock_.current_wall_time() << "ms cpu=" << profile_clock_.current_process_time() << "ms";
        return stream.str();
    }

    void report_formatter::append_statistics(std::ostringstream& stream, const solver_statistics::snapshot& snapshot) noexcept
    {
        append_counter(stream, counter_id::conflicts, snapshot.conflicts);
        append_counter(stream, counter_id::decisions, snapshot.decisions);
        append_counter(stream, counter_id::propagations, snapshot.propagations);
        append_counter(stream, counter_id::restarts, snapshot.restarts);
        append_counter(stream, counter_id::learned_clauses, snapshot.learned_clauses);
        append_counter(stream, counter_id::learned_clause_glue_total, snapshot.learned_clause_glue_total);
        append_counter(stream, counter_id::learned_clause_glue_samples, snapshot.learned_clause_glue_samples);
        append_counter(stream, counter_id::reduction_passes, snapshot.reduction_passes);
        append_counter(stream, counter_id::reduced_clauses, snapshot.reduced_clauses);
        append_counter(stream, counter_id::deleted_clauses, snapshot.deleted_clauses);
    }

    void report_formatter::append_counter(std::ostringstream& stream, const counter_id id, const counter_t value) noexcept
    {
        if (stream.tellp() != std::streampos {0L})
            stream << ' ';
        stream << name_of(id) << '=' << value;
    }
}
