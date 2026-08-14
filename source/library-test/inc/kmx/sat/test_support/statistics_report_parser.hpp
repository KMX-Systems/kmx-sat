#pragma once
#ifndef PCH
    #include <cstdint>
    #include <sstream>
    #include <string>
    #include <string_view>
    #include <unordered_map>
#endif

namespace kmx::sat::test_support
{
    using statistics_report_map = std::unordered_map<std::string, std::uint64_t>;

    inline statistics_report_map parse_statistics_report_line(const std::string_view line)
    {
        statistics_report_map parsed {};
        std::istringstream stream {std::string {line}};
        std::string token {};
        while (stream >> token)
        {
            const auto equals_position = token.find('=');
            if (equals_position == std::string::npos || equals_position == 0u || equals_position + 1u >= token.size())
                continue;

            const auto key = token.substr(0u, equals_position);
            const auto value_text = token.substr(equals_position + 1u);
            try
            {
                parsed[key] = static_cast<std::uint64_t>(std::stoull(value_text));
            }
            catch (...)
            {
            }
        }
        return parsed;
    }
}
