/// @file inc/kmx/sat/io/dimacs_parser.hpp
/// @brief Incremental/streaming CNF parser.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cctype>
    #include <cstdint>
    #include <cstdlib>
    #include <span>
    #include <sstream>
    #include <string>
    #include <string_view>
    #include <vector>
#endif
#include <kmx/sat/io/file_source.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::io
{
    /// @brief Incremental/streaming CNF parser.
    ///
    /// @details
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
            reset_state();

            std::string text {};
            char buffer[4096] {};
            for (;;)
            {
                const auto read_count = source.read(buffer, sizeof(buffer));
                if (read_count == 0)
                    break;
                text.append(buffer, read_count);
            }

            if (text.empty())
            {
                error_context_ = "empty input";
                return false;
            }

            std::vector<literal> clause_buffer {};
            std::size_t line_start = 0;
            while (line_start <= text.size())
            {
                const auto line_end = text.find('\n', line_start);
                const auto line_length = line_end == std::string::npos ? text.size() - line_start : line_end - line_start;
                std::string_view line_view {text.data() + line_start, line_length};
                line_start = line_end == std::string::npos ? text.size() + 1 : line_end + 1;

                while (!line_view.empty() && std::isspace(static_cast<unsigned char>(line_view.front())) != 0)
                    line_view.remove_prefix(1);
                while (!line_view.empty() && std::isspace(static_cast<unsigned char>(line_view.back())) != 0)
                    line_view.remove_suffix(1);

                if (line_view.empty())
                    continue;
                if (line_view.front() == 'c')
                    continue;

                if (line_view.front() == 'p')
                {
                    if (header_parsed_)
                    {
                        error_context_ = "duplicate header";
                        return false;
                    }

                    std::stringstream line_stream {std::string {line_view}};
                    std::string p_token {};
                    std::string format_token {};
                    line_stream >> p_token >> format_token >> declared_variable_count_ >> declared_clause_count_;
                    if (!line_stream || p_token != "p" || format_token != "cnf")
                    {
                        error_context_ = "invalid header";
                        return false;
                    }
                    header_parsed_ = true;
                    continue;
                }

                if (!header_parsed_)
                {
                    error_context_ = "missing header";
                    return false;
                }

                std::stringstream line_stream {std::string {line_view}};
                std::int64_t token {};
                while (line_stream >> token)
                {
                    if (token == 0)
                    {
                        clauses_.push_back(clause_buffer);
                        clause_buffer.clear();
                        continue;
                    }

                    const auto variable_index = static_cast<std::uint32_t>(std::llabs(token));
                    if (variable_index == 0 || variable_index > declared_variable_count_)
                    {
                        error_context_ = "literal out of domain";
                        return false;
                    }

                    clause_buffer.push_back(literal {variable {variable_index}, token < 0});
                }

                if (!line_stream.eof())
                {
                    error_context_ = "invalid literal token";
                    return false;
                }
            }

            if (!header_parsed_)
            {
                error_context_ = "missing header";
                return false;
            }

            if (!clause_buffer.empty())
            {
                error_context_ = "unterminated clause";
                return false;
            }

            if (clauses_.size() != declared_clause_count_)
            {
                error_context_ = "header clause count mismatch";
                return false;
            }

            return true;
        }

        /// @brief Parses and validates the `p cnf <num_vars> <num_clauses>` header line.
        /// @param source File source positioned at the header.
        /// @return True if the header is well-formed and consistent.
        /// @throws None (noexcept).
        bool parse_header(file_source& source) noexcept
        {
            reset_state();

            std::string line {};
            int ch = source.getc();
            while (ch >= 0)
            {
                const char c = static_cast<char>(ch);
                if (c == '\n')
                {
                    std::string_view line_view {line};
                    while (!line_view.empty() && std::isspace(static_cast<unsigned char>(line_view.front())) != 0)
                        line_view.remove_prefix(1);

                    if (!line_view.empty() && line_view.front() == 'p')
                    {
                        std::stringstream line_stream {std::string {line_view}};
                        std::string p_token {};
                        std::string format_token {};
                        line_stream >> p_token >> format_token >> declared_variable_count_ >> declared_clause_count_;
                        if (line_stream && p_token == "p" && format_token == "cnf")
                        {
                            header_parsed_ = true;
                            return true;
                        }
                        error_context_ = "invalid header";
                        return false;
                    }

                    line.clear();
                }
                else
                {
                    line.push_back(c);
                }
                ch = source.getc();
            }

            error_context_ = "missing header";
            return false;
        }

        /// @brief Parses and validates one clause line, including literal domain checks.
        /// @param source File source positioned at a clause line.
        /// @return True if the clause line is well-formed and within the declared literal domain.
        /// @throws None (noexcept).
        bool parse_clause(file_source& source) noexcept
        {
            if (!parse(source))
                return false;
            return !clauses_.empty();
        }

        /// @brief Returns diagnostic context (location, failing token, violated rule) for the last parse failure.
        /// @return Human-readable error context string.
        /// @throws None (noexcept).
        std::string_view report_error_context() const noexcept { return error_context_; }

        /// @brief Returns the variable count declared in the parsed DIMACS header.
        /// @return Declared variable count.
        /// @throws None (noexcept).
        std::uint32_t declared_variable_count() const noexcept { return declared_variable_count_; }

        /// @brief Returns the clause count declared in the parsed DIMACS header.
        /// @return Declared clause count.
        /// @throws None (noexcept).
        std::uint32_t declared_clause_count() const noexcept { return declared_clause_count_; }

        /// @brief Exposes parsed clauses as internal literals in input order.
        /// @return Read-only view of parsed clause vectors.
        /// @throws None (noexcept).
        std::span<const std::vector<literal>> clauses() const noexcept { return clauses_; }

    private:
        void reset_state() noexcept
        {
            header_parsed_ = false;
            declared_variable_count_ = 0;
            declared_clause_count_ = 0;
            clauses_.clear();
            error_context_.clear();
        }

        bool header_parsed_ {};
        std::uint32_t declared_variable_count_ {};
        std::uint32_t declared_clause_count_ {};
        std::vector<std::vector<literal>> clauses_ {};
        std::string error_context_ {};
    };
}
