/// @file inc/kmx/sat/test_support/statistics_report_parser.hpp
/// @brief Parser turning a solver statistics report into a counter-keyed map for assertions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <sstream>
    #include <string>
    #include <string_view>
    #include <unordered_map>
#endif
#include <kmx/sat/telemetry/solver_statistics.hpp>

namespace kmx::sat::test_support
{
    using statistics_report_map_t = std::unordered_map<telemetry::counter_id, std::uint64_t>;

    /// @brief Parses one report line into the counters it carries.
    /// @details The report is text, so this is where field names become `telemetry::counter_id` values; fields that
    /// name no counter (the resource line's `time`/`cpu`, for instance) are dropped rather than kept as text.
    inline statistics_report_map_t parse_statistics_report_line(const std::string_view line)
    {
        statistics_report_map_t parsed {};
        std::istringstream stream {std::string {line}};
        std::string token {};
        while (stream >> token)
        {
            const auto equals_position = token.find('=');
            if ((equals_position == std::string::npos) || (equals_position == 0u) || (equals_position + 1u >= token.size()))
                continue;

            const auto id = telemetry::parse_counter_id(std::string_view {token}.substr(0u, equals_position));
            if (!id.has_value())
                continue;

            const auto value_text = token.substr(equals_position + 1u);
            try
            {
                parsed[id.value()] = static_cast<std::uint64_t>(std::stoull(value_text));
            }
            catch (...)
            {
            }
        }
        return parsed;
    }
}
