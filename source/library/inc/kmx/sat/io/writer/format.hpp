/// @file inc/kmx/sat/io/writer/format.hpp
/// @brief Text/binary serialization for proof and reporting.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
    #include <string>
    #include <string_view>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/telemetry/report_formatter.hpp>

namespace kmx::sat::io::writer
{
    /// @brief Text/binary serialization for proof and reporting.
    ///
    /// @details
    /// `writer::format` is the shared low-level text/binary serialization primitive used by both the proof output
    /// path and human-facing reporting: `write_clause` serializes a literal span in the encoding a given proof format
    /// tracer requires; `write_report_line` writes a formatted diagnostic/progress line (used by
    /// `telemetry::report_formatter`); `write_statistics` serializes a statistics snapshot; `write_proof_record`
    /// writes one already-assembled proof record (add/delete/shrink) produced by a concrete tracer. It is a plain
    /// concrete type with no runtime-selected alternatives, requiring neither virtual dispatch nor type erasure.
    class format final
    {
    public:
        enum class statistics_detail : std::uint8_t
        {
            compact,
            verbose
        };

        /// @brief Constructs a format writer with no open output target.
        /// @throws None (noexcept).
        format() noexcept = default;

        /// @brief Serializes a clause's literals in the target output encoding.
        /// @param clause Read-only span of literals to serialize.
        /// @throws None (noexcept).
        void write_clause(const std::span<const literal> clause) noexcept
        {
            for (const auto lit: clause)
            {
                const auto signed_value =
                    lit.is_negated() ? -static_cast<int>(lit.variable_of().index()) : static_cast<int>(lit.variable_of().index());
                buffer_.append(std::to_string(signed_value));
                buffer_.push_back(' ');
            }
            buffer_.append("0\n");
        }

        /// @brief Serializes a statistics snapshot to the output target.
        /// @throws None (noexcept).
        void write_statistics() noexcept { buffer_.append("statistics\n"); }

        /// @brief Serializes one statistics snapshot as a formatted report line.
        /// @param snapshot Statistics counters snapshot to serialize.
        /// @param detail Controls compact vs verbose rendering.
        /// @throws None (noexcept).
        void write_statistics(const telemetry::solver_statistics::snapshot& snapshot,
                              const statistics_detail detail = statistics_detail::compact) noexcept
        {
            const telemetry::report_formatter formatter {};
            if (detail == statistics_detail::verbose)
            {
                write_report_line(formatter.format_statistics_line_verbose(snapshot));
                return;
            }
            write_report_line(formatter.format_statistics_line(snapshot));
        }

        /// @brief Writes one formatted diagnostic/progress report line.
        /// @param line Line content to write.
        /// @throws None (noexcept).
        void write_report_line(const std::string_view line) noexcept
        {
            buffer_.append(line.begin(), line.end());
            if (buffer_.empty() || buffer_.back() != '\n')
                buffer_.push_back('\n');
        }

        /// @brief Writes one already-assembled proof record (add/delete/shrink) to the output target.
        /// @throws None (noexcept).
        void write_proof_record() noexcept { buffer_.append("proof-record\n"); }

        /// @brief Clears the currently accumulated serialized output.
        /// @throws None (noexcept).
        void clear() noexcept { buffer_.clear(); }

        /// @brief Exposes the accumulated serialized output.
        /// @return Current serialized output buffer.
        /// @throws None (noexcept).
        std::string_view buffer_view() const noexcept { return buffer_; }

    private:
        std::string buffer_ {};
    };
}
