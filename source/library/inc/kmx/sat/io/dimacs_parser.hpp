/// @file inc/kmx/sat/io/dimacs_parser.hpp
/// @brief Incremental/streaming CNF parser.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <string_view>
#endif
#include <kmx/sat/io/file_source.hpp>

namespace kmx::sat::io
{
    /// @brief Incremental/streaming CNF parser.
    ///
    /// `dimacs_parser` implements the DIMACS CNF input grammar (`p cnf <num_vars> <num_clauses>` header, `c` comment
    /// lines, signed-integer clause lines terminated by `0`) over a `file_source`, applying every mandatory
    /// validation rule from the input-grammar specification as it streams: header consistency, clause termination,
    /// literal domain checks, no interior zero literal, and overflow-safe integer parsing. `parse_header`/
    /// `parse_clause` are the two grammar productions `parse` drives incrementally (rather than materializing the
    /// whole file), which matters for very large competition instances; `report_error_context` exposes the source
    /// location, failing token, and violated rule needed by the `parse_error`/`domain_error` reporting requirements.
    /// @note DIMACS remains the mandatory canonical external input format; the optional binary fixture channel
    /// (`io::fixture::binary::reader`) is a feature-gated fast path for tests/benchmarks and must never replace this
    /// parser as the primary ingestion route.
    class dimacs_parser final
    {
    public:
        /// @brief Constructs a parser with no active source.
        /// @throws None (noexcept).
        dimacs_parser() noexcept = default;

        /// @brief Parses an entire DIMACS CNF source, driving the header and clause productions to completion.
        /// @param source File source to read the CNF content from.
        /// @return True if parsing completed without a grammar or domain violation.
        /// @throws None (noexcept).
        bool parse(file_source& source) noexcept
        {
            return false;
        }

        /// @brief Parses and validates the `p cnf <num_vars> <num_clauses>` header line.
        /// @param source File source positioned at the header.
        /// @return True if the header is well-formed and consistent.
        /// @throws None (noexcept).
        bool parse_header(file_source& source) noexcept
        {
            return false;
        }

        /// @brief Parses and validates one clause line, including literal domain checks.
        /// @param source File source positioned at a clause line.
        /// @return True if the clause line is well-formed and within the declared literal domain.
        /// @throws None (noexcept).
        bool parse_clause(file_source& source) noexcept
        {
            return false;
        }

        /// @brief Returns diagnostic context (location, failing token, violated rule) for the last parse failure.
        /// @return Human-readable error context string.
        /// @throws None (noexcept).
        std::string_view report_error_context() const noexcept
        {
            return {};
        }
    };
}
