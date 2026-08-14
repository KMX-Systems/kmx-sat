#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::test_support
{
    enum class replay_operation_kind : std::uint8_t
    {
        add_clause,
        assume,
        release_assumptions,
        solve,
        reset_session,
        set_option,
        value_of,
        failed,
    };

    struct incremental_replay_operation final
    {
        replay_operation_kind kind {};
        std::vector<literal> literals {};
        std::uint64_t conflict_limit {};
        std::uint64_t decision_limit {};
        std::string option_name {};
        std::int64_t option_value {};
        variable variable_operand {};
        literal literal_operand {};
    };

    struct incremental_replay_trace final
    {
        static constexpr std::uint32_t schema_version {1u};

        std::uint32_t seed {};
        std::uint32_t variable_count {};
        std::vector<incremental_replay_operation> operations {};

        std::string serialize_json_lines() const
        {
            std::ostringstream output;
            output << "{\"schema\":" << schema_version << ",\"seed\":" << seed << ",\"variable_count\":" << variable_count
                   << ",\"kind\":\"header\"}\n";
            for (const auto& operation: operations)
            {
                output << "{\"kind\":\"" << kind_name(operation.kind) << "\"";
                if (!operation.literals.empty())
                {
                    output << ",\"literals\":[";
                    for (std::size_t index {}; index < operation.literals.size(); ++index)
                    {
                        if (index != 0u)
                            output << ',';
                        output << dimacs_value(operation.literals[index]);
                    }
                    output << ']';
                }
                if (operation.kind == replay_operation_kind::solve)
                    output << ",\"conflict_limit\":" << operation.conflict_limit << ",\"decision_limit\":" << operation.decision_limit;
                if (operation.kind == replay_operation_kind::set_option)
                    output << ",\"option\":\"" << escaped(operation.option_name) << "\",\"value\":" << operation.option_value;
                if (operation.kind == replay_operation_kind::value_of)
                    output << ",\"variable\":" << operation.variable_operand.index();
                if (operation.kind == replay_operation_kind::failed)
                    output << ",\"literal\":" << dimacs_value(operation.literal_operand);
                output << "}\n";
            }
            return output.str();
        }

        static std::optional<incremental_replay_trace> parse_json_lines(const std::string_view text)
        {
            std::istringstream input {std::string {text}};
            std::string line;
            incremental_replay_trace trace;
            bool header_seen = false;
            while (std::getline(input, line))
            {
                if (line.empty())
                    continue;
                const auto kind = string_field(line, "kind");
                if (!kind.has_value())
                    return {};
                if (kind.value() == "header")
                {
                    if (header_seen || number_field(line, "schema") != schema_version)
                        return {};
                    const auto seed = number_field(line, "seed");
                    const auto variables = number_field(line, "variable_count");
                    if (!seed.has_value() || !variables.has_value())
                        return {};
                    trace.seed = static_cast<std::uint32_t>(seed.value());
                    trace.variable_count = static_cast<std::uint32_t>(variables.value());
                    header_seen = true;
                    continue;
                }
                if (!header_seen)
                    return {};

                incremental_replay_operation operation;
                const auto parsed_kind = parse_kind(kind.value());
                if (!parsed_kind.has_value())
                    return {};
                operation.kind = parsed_kind.value();
                switch (operation.kind)
                {
                    case replay_operation_kind::add_clause:
                    case replay_operation_kind::assume:
                    {
                        const auto literals = literal_array_field(line, "literals");
                        if (!literals.has_value() || literals->empty())
                            return {};
                        operation.literals = literals.value();
                        break;
                    }
                    case replay_operation_kind::solve:
                    {
                        const auto conflicts = number_field(line, "conflict_limit");
                        const auto decisions = number_field(line, "decision_limit");
                        if (!conflicts.has_value() || !decisions.has_value())
                            return {};
                        operation.conflict_limit = conflicts.value();
                        operation.decision_limit = decisions.value();
                        break;
                    }
                    case replay_operation_kind::set_option:
                    {
                        const auto option = string_field(line, "option");
                        const auto value = signed_number_field(line, "value");
                        if (!option.has_value() || !value.has_value())
                            return {};
                        operation.option_name = option.value();
                        operation.option_value = static_cast<std::int64_t>(value.value());
                        break;
                    }
                    case replay_operation_kind::value_of:
                    {
                        const auto variable_value = number_field(line, "variable");
                        if (!variable_value.has_value())
                            return {};
                        operation.variable_operand = variable {static_cast<variable::index_t>(variable_value.value())};
                        break;
                    }
                    case replay_operation_kind::failed:
                    {
                        const auto literal_value = signed_number_field(line, "literal");
                        if (!literal_value.has_value() || literal_value.value() == 0)
                            return {};
                        operation.literal_operand = from_dimacs(literal_value.value());
                        break;
                    }
                    default:
                        break;
                }
                trace.operations.push_back(std::move(operation));
            }
            return header_seen ? std::optional<incremental_replay_trace> {std::move(trace)} : std::nullopt;
        }

    private:
        static std::optional<replay_operation_kind> parse_kind(const std::string_view value) noexcept
        {
            static constexpr std::array<std::pair<std::string_view, replay_operation_kind>, 8> kinds {{
                {"add_clause", replay_operation_kind::add_clause},
                {"assume", replay_operation_kind::assume},
                {"failed", replay_operation_kind::failed},
                {"release_assumptions", replay_operation_kind::release_assumptions},
                {"reset_session", replay_operation_kind::reset_session},
                {"set_option", replay_operation_kind::set_option},
                {"solve", replay_operation_kind::solve},
                {"value_of", replay_operation_kind::value_of},
            }};
            const auto it = std::lower_bound(kinds.begin(), kinds.end(), value,
                [](const auto& pair, const std::string_view v) { return pair.first < v; });
            if (it != kinds.end() && it->first == value)
                return it->second;
            return {};
        }

        static std::optional<std::uint64_t> number_field(const std::string_view line, const std::string_view field) noexcept
        {
            const std::string marker = "\"" + std::string {field} + "\":";
            const auto start = line.find(marker);
            if (start == std::string_view::npos)
                return {};
            const auto first = start + marker.size();
            const auto last = line.find_first_of(",}", first);
            const auto token = line.substr(first, last == std::string_view::npos ? line.size() - first : last - first);
            std::uint64_t value {};
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            return result.ec == std::errc {} && result.ptr == token.data() + token.size() ? std::optional {value} : std::nullopt;
        }

        static std::optional<std::int64_t> signed_number_field(const std::string_view line, const std::string_view field) noexcept
        {
            const std::string marker = "\"" + std::string {field} + "\":";
            const auto start = line.find(marker);
            if (start == std::string_view::npos)
                return {};
            const auto first = start + marker.size();
            const auto last = line.find_first_of(",}", first);
            const auto token = line.substr(first, last == std::string_view::npos ? line.size() - first : last - first);
            std::int64_t value {};
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            return result.ec == std::errc {} && result.ptr == token.data() + token.size() ? std::optional {value} : std::nullopt;
        }

        static std::optional<std::string> string_field(const std::string_view line, const std::string_view field)
        {
            const std::string marker = "\"" + std::string {field} + "\":\"";
            const auto start = line.find(marker);
            if (start == std::string_view::npos)
                return {};
            const auto first = start + marker.size();
            const auto last = line.find('"', first);
            return last == std::string_view::npos ? std::nullopt : std::optional {std::string {line.substr(first, last - first)}};
        }

        static std::optional<std::vector<literal>> literal_array_field(const std::string_view line, const std::string_view field)
        {
            const std::string marker = "\"" + std::string {field} + "\":[";
            const auto start = line.find(marker);
            if (start == std::string_view::npos)
                return {};
            const auto first = start + marker.size();
            const auto last = line.find(']', first);
            if (last == std::string_view::npos)
                return {};
            std::vector<literal> literals;
            std::string values {line.substr(first, last - first)};
            std::istringstream tokens {values};
            std::string token;
            while (std::getline(tokens, token, ','))
            {
                std::int64_t value {};
                const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
                if (result.ec != std::errc {} || result.ptr != token.data() + token.size() || value == 0)
                    return {};
                literals.push_back(from_dimacs(value));
            }
            return literals;
        }

        static literal from_dimacs(const std::int64_t value) noexcept
        {
            const auto magnitude = static_cast<variable::index_t>(value < 0 ? -value : value);
            return literal {variable {magnitude}, value < 0};
        }

        static std::string_view kind_name(const replay_operation_kind kind) noexcept
        {
            switch (kind)
            {
                case replay_operation_kind::add_clause:
                    return "add_clause";
                case replay_operation_kind::assume:
                    return "assume";
                case replay_operation_kind::release_assumptions:
                    return "release_assumptions";
                case replay_operation_kind::solve:
                    return "solve";
                case replay_operation_kind::reset_session:
                    return "reset_session";
                case replay_operation_kind::set_option:
                    return "set_option";
                case replay_operation_kind::value_of:
                    return "value_of";
                case replay_operation_kind::failed:
                    return "failed";
            }
            return "unknown";
        }

        static std::int64_t dimacs_value(const literal value) noexcept
        {
            const auto variable_value = static_cast<std::int64_t>(value.variable_of().index());
            return value.is_negated() ? -variable_value : variable_value;
        }

        static std::string escaped(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char character: value)
            {
                if (character == '\\' || character == '"')
                    result.push_back('\\');
                result.push_back(character);
            }
            return result;
        }
    };
}
