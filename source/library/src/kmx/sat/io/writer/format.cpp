/// @file library/src/kmx/sat/io/writer/format.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/writer/format.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/writer/format.hpp>

namespace kmx::sat::io::writer
{
    void format::write_clause(const std::span<const literal> clause) noexcept
    {
        std::array<char, 32u> number {};
        for (const auto lit: clause)
        {
            const auto variable_index = static_cast<std::int64_t>(lit.variable_of().index());
            const auto signed_value = lit.is_negated() ? -variable_index : variable_index;
            number.fill('\0');
            const auto result = std::to_chars(number.data(), number.data() + number.size(), signed_value);
            buffer_.append(number.data(), static_cast<std::size_t>(result.ptr - number.data()));
            buffer_.push_back(' ');
        }
        buffer_.append("0\n");
    }

    void format::write_statistics(const telemetry::solver_statistics::snapshot& snapshot, const statistics_detail detail) noexcept
    {
        const telemetry::report_formatter formatter {};
        if (detail == statistics_detail::verbose)
        {
            write_report_line(formatter.format_statistics_line_verbose(snapshot));
            return;
        }
        write_report_line(formatter.format_statistics_line(snapshot));
    }

    void format::write_report_line(const std::string_view line) noexcept
    {
        buffer_.append(line.begin(), line.end());
        if (buffer_.empty() || (buffer_.back() != '\n'))
            buffer_.push_back('\n');
    }

    void format::write_report_line(const char line) noexcept
    {
        buffer_.push_back(line);
        if (line != '\n')
            buffer_.push_back('\n');
    }
}
