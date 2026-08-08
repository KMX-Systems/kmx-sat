/// @file inc/kmx/sat/io/writer/format.hpp
/// @brief Text/binary serialization for proof and reporting.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
    #include <string_view>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat::io::writer
{
    /// @brief Text/binary serialization for proof and reporting.
    ///
    /// `writer::format` is the shared low-level text/binary serialization primitive used by both the proof output
    /// path and human-facing reporting: `write_clause` serializes a literal span in the encoding a given proof format
    /// tracer requires; `write_report_line` writes a formatted diagnostic/progress line (used by
    /// `telemetry::report_formatter`); `write_statistics` serializes a statistics snapshot; `write_proof_record`
    /// writes one already-assembled proof record (add/delete/shrink) produced by a concrete tracer. It is a plain
    /// concrete type with no runtime-selected alternatives, requiring neither virtual dispatch nor type erasure.
    class format final
    {
    public:
        /// @brief Constructs a format writer with no open output target.
        /// @throws None (noexcept).
        format() noexcept = default;

        /// @brief Serializes a clause's literals in the target output encoding.
        /// @param clause Read-only span of literals to serialize.
        /// @throws None (noexcept).
        void write_clause(const std::span<const literal> clause) noexcept
        {
        }

        /// @brief Serializes a statistics snapshot to the output target.
        /// @throws None (noexcept).
        void write_statistics() noexcept
        {
        }

        /// @brief Writes one formatted diagnostic/progress report line.
        /// @param line Line content to write.
        /// @throws None (noexcept).
        void write_report_line(const std::string_view line) noexcept
        {
        }

        /// @brief Writes one already-assembled proof record (add/delete/shrink) to the output target.
        /// @throws None (noexcept).
        void write_proof_record() noexcept
        {
        }
    };
}
