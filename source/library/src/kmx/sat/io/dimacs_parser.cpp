/// @file library/src/kmx/sat/io/dimacs_parser.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/dimacs_parser.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/dimacs_parser.hpp>

namespace kmx::sat::io
{
    bool dimacs_parser::parse(file_source& source) noexcept
    {
        reset_state();

        std::string text {};
        std::array<char, 4096u> buffer {};
        for (;;)
        {
            const auto read_count = source.read(buffer.data(), buffer.size());
            if (read_count == 0u)
                break;
            text.append(buffer.data(), read_count);
        }

        if (text.empty())
        {
            error_context_ = "empty input";
            return false;
        }

        std::vector<literal> clause_buffer {};
        clause_buffer.reserve(1024u);
        std::size_t line_start {};
        while (line_start <= text.size())
        {
            const auto line_end = text.find('\n', line_start);
            const auto line_length = (line_end == std::string::npos) ? text.size() - line_start : line_end - line_start;
            std::string_view line_view {text.data() + line_start, line_length};
            line_start = (line_end == std::string::npos) ? text.size() + 1u : line_end + 1u;

            while (!line_view.empty() && (std::isspace(static_cast<unsigned char>(line_view.front())) != 0))
                line_view.remove_prefix(1u);
            while (!line_view.empty() && (std::isspace(static_cast<unsigned char>(line_view.back())) != 0))
                line_view.remove_suffix(1u);

            if (line_view.empty())
                continue;
            switch (line_view.front())
            {
                case 'c':
                    continue;
                case 'p':
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
                    if (!line_stream || (p_token != "p") || (format_token != "cnf"))
                    {
                        error_context_ = "invalid header";
                        return false;
                    }
                    header_parsed_ = true;
                    continue;
                }
                default:
                    break;
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
                if (token == 0L)
                {
                    clauses_.push_back(clause_buffer);
                    clause_buffer.clear();
                    continue;
                }

                const auto variable_index = static_cast<std::uint32_t>(std::llabs(token));
                if ((variable_index == 0u) || (variable_index > declared_variable_count_))
                {
                    error_context_ = "literal out of domain";
                    return false;
                }

                clause_buffer.push_back(literal {variable {variable_index}, token < 0L});
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

    bool dimacs_parser::parse_header(file_source& source) noexcept
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
                while (!line_view.empty() && (std::isspace(static_cast<unsigned char>(line_view.front())) != 0))
                    line_view.remove_prefix(1u);

                if (!line_view.empty() && (line_view.front() == 'p'))
                {
                    std::stringstream line_stream {std::string {line_view}};
                    std::string p_token {};
                    std::string format_token {};
                    line_stream >> p_token >> format_token >> declared_variable_count_ >> declared_clause_count_;
                    if (line_stream && (p_token == "p") && (format_token == "cnf"))
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

    bool dimacs_parser::parse_clause(file_source& source) noexcept
    {
        if (!parse(source))
            return false;
        return !clauses_.empty();
    }

    void dimacs_parser::reset_state() noexcept
    {
        header_parsed_ = false;
        declared_variable_count_ = 0u;
        declared_clause_count_ = 0u;
        clauses_.clear();
        error_context_.clear();
    }
}
