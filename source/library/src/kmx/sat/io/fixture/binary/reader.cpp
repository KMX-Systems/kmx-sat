/// @file library/src/kmx/sat/io/fixture/binary/reader.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/fixture/binary/reader.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/fixture/binary/reader.hpp>

namespace kmx::sat::io::fixture::binary
{
    bool reader::open_fixture(file_source& source) noexcept
    {
        close_fixture();
        source_ = &source;
        return true;
    }

    bool reader::read_header() noexcept
    {
        if (source_ == nullptr)
            return false;

        raw_fixture_.clear();
        std::array<char, 4096u> buffer {};
        for (;;)
        {
            const auto read_count = source_->read(buffer.data(), buffer.size());
            if (read_count == 0u)
                break;
            raw_fixture_.append(buffer.data(), read_count);
        }

        if (raw_fixture_.empty())
            return false;

        const auto newline = raw_fixture_.find('\n');
        if (newline == std::string::npos)
            return false;

        std::string_view header {raw_fixture_.data(), newline};
        return parse_header_line(header);
    }

    bool reader::load_payload() noexcept
    {
        if (raw_fixture_.empty())
            return false;

        clauses_.clear();
        assumptions_.clear();
        request_ = solve_request {};
        request_.conflict_limit = conflict_limit_;
        request_.decision_limit = decision_limit_;
        request_.enabled_pass_mask = enabled_pass_mask_;
        request_.strict_mode = strict_mode_;
        std::size_t parsed_clause_count {};
        std::size_t parsed_assumption_count {};

        const auto newline = raw_fixture_.find('\n');
        std::size_t line_start = (newline == std::string::npos) ? raw_fixture_.size() : newline + 1u;
        while (line_start < raw_fixture_.size())
        {
            const auto line_end = raw_fixture_.find('\n', line_start);
            const auto line_length = (line_end == std::string::npos) ? raw_fixture_.size() - line_start : line_end - line_start;
            if (line_length == 1u)
            {
                std::string_view line {raw_fixture_.data() + line_start, line_length};
                line_start = (line_end == std::string::npos) ? raw_fixture_.size() : line_end + 1u;
                switch (line.front())
                {
                    case 'c':
                    {
                        const auto clause_end = raw_fixture_.find('\n', line_start);
                        if (clause_end == std::string::npos)
                            return false;
                        std::string_view clause_line {raw_fixture_.data() + line_start, clause_end - line_start};
                        auto clause = parse_clause_line(clause_line);
                        if (!clause.has_value() || clause->empty())
                            return false;
                        clauses_.push_back(std::move(*clause));
                        ++parsed_clause_count;
                        line_start = clause_end + 1u;
                        continue;
                    }
                    case 'a':
                    {
                        const auto assumptions_end = raw_fixture_.find('\n', line_start);
                        if (assumptions_end == std::string::npos)
                            return false;
                        std::string_view assumptions_line {raw_fixture_.data() + line_start, assumptions_end - line_start};
                        auto parsed_assumptions = parse_clause_line(assumptions_line);
                        if (!parsed_assumptions.has_value())
                            return false;
                        assumptions_ = std::move(*parsed_assumptions);
                        parsed_assumption_count = assumptions_.size();
                        request_.assumptions = assumptions_;
                        line_start = assumptions_end + 1u;
                        continue;
                    }
                    default:;
                }
            }
        }

        if ((parsed_clause_count != clause_count_) || (parsed_assumption_count != assumption_count_))
        {
            clauses_.clear();
            assumptions_.clear();
            request_ = solve_request {};
            return false;
        }

        validator_.set_declared_variable_count(declared_variable_count_);
        validator_.set_clauses(clauses_);
        validator_.set_assumptions(assumptions_);
        validator_.set_limits_payload(request_);
        return true;
    }

    void reader::materialize_fixture_into_frontend(cdcl::external_frontend& frontend) noexcept
    {
        if (!validate_payload())
            return;
        frontend.clear_clauses();
        frontend.clear_assumptions();
        if (schema_.payload_kind_of() != schema::payload_kind::solve_request_fixture)
        {
            for (const auto& clause: clauses_)
                frontend.push_clause(clause);
            return;
        }

        for (const auto lit: assumptions_)
            frontend.push_assumption(lit);

        solve_request prepared_request {};
        prepared_request.conflict_limit = request_.conflict_limit;
        prepared_request.decision_limit = request_.decision_limit;
        prepared_request.enabled_pass_mask = request_.enabled_pass_mask;
        prepared_request.strict_mode = request_.strict_mode;
        prepared_request.assumptions = std::vector<literal> {assumptions_.begin(), assumptions_.end()};
        frontend.apply_prepared_request(prepared_request);
    }

    void reader::close_fixture() noexcept
    {
        source_ = nullptr;
        raw_fixture_.clear();
        clauses_.clear();
        assumptions_.clear();
        request_ = solve_request {};
    }

    std::string_view reader::trim(std::string_view input) noexcept
    {
        while (!input.empty() && (input.front() == ' ' || input.front() == '\t' || input.front() == '\r'))
            input.remove_prefix(1u);
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r'))
            input.remove_suffix(1u);
        return input;
    }

    bool reader::parse_unsigned(std::string_view token, std::uint64_t& value) noexcept
    {
        token = trim(token);
        if (token.empty())
            return false;

        std::uint64_t parsed {};
        const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
        if ((result.ec != std::errc {}) || (result.ptr != token.data() + token.size()))
            return false;
        value = parsed;
        return true;
    }

    bool reader::parse_signed(std::string_view token, std::int64_t& value) noexcept
    {
        token = trim(token);
        if (token.empty())
            return false;

        std::int64_t parsed {};
        const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
        if ((result.ec != std::errc {}) || (result.ptr != token.data() + token.size()))
            return false;
        value = parsed;
        return true;
    }

    bool reader::parse_header_line(const std::string_view header) noexcept
    {
        std::array<std::string_view, 12u> tokens {};
        std::size_t token_count {};
        std::size_t start {};
        while (start < header.size())
        {
            while ((start < header.size()) && (header[start] == ' '))
                ++start;
            if (start >= header.size())
                break;
            const auto end = header.find(' ', start);
            const auto token_end = (end == std::string::npos) ? header.size() : end;
            if (token_count >= tokens.size())
                return false;
            tokens[token_count++] = header.substr(start, token_end - start);
            start = (token_end == header.size()) ? header.size() : token_end + 1u;
        }

        if (token_count != tokens.size())
            return false;

        std::uint64_t parsed_version {};
        std::uint64_t kind_value {};
        std::uint64_t flags_value {};
        std::uint64_t declared_vars {};
        std::uint64_t parsed_clause_count {};
        std::uint64_t parsed_assumption_count {};
        std::uint64_t parsed_conflict_limit {};
        std::uint64_t parsed_decision_limit {};
        std::uint64_t parsed_pass_mask {};
        std::uint64_t parsed_strict_mode {};
        std::uint64_t parsed_checksum {};

        if ((tokens[0u] != "SATB") || !parse_unsigned(tokens[1u], parsed_version) || !parse_unsigned(tokens[2u], kind_value) ||
            !parse_unsigned(tokens[3u], flags_value) || !parse_unsigned(tokens[4u], declared_vars) ||
            !parse_unsigned(tokens[5u], parsed_clause_count) || !parse_unsigned(tokens[6u], parsed_assumption_count) ||
            !parse_unsigned(tokens[7u], parsed_conflict_limit) || !parse_unsigned(tokens[8u], parsed_decision_limit) ||
            !parse_unsigned(tokens[9u], parsed_pass_mask) || !parse_unsigned(tokens[10u], parsed_strict_mode) ||
            !parse_unsigned(tokens[11u], parsed_checksum))
        {
            return false;
        }

        if (kind_value > static_cast<std::uint64_t>(schema::payload_kind::solve_request_fixture))
            return false;

        version_ = static_cast<std::uint16_t>(parsed_version);
        declared_variable_count_ = static_cast<std::uint32_t>(declared_vars);
        clause_count_ = static_cast<std::size_t>(parsed_clause_count);
        assumption_count_ = static_cast<std::size_t>(parsed_assumption_count);
        conflict_limit_ = parsed_conflict_limit;
        decision_limit_ = parsed_decision_limit;
        enabled_pass_mask_ = parsed_pass_mask;
        strict_mode_ = parsed_strict_mode != 0u;
        declared_checksum_ = parsed_checksum;
        schema_ = schema {};
        schema_.set_payload_kind(static_cast<schema::payload_kind>(kind_value));
        schema_.set_feature_flags(flags_value);
        validator_.set_header(tokens[0u], version_, declared_checksum_, schema_);
        return true;
    }

    optional_clause_t reader::parse_clause_line(const std::string_view line) noexcept
    {
        std::vector<literal> result {};
        std::size_t start {};
        bool terminated {};
        while (start < line.size())
        {
            while ((start < line.size()) && (line[start] == ' '))
                ++start;
            if (start >= line.size())
                break;
            const auto end = line.find(' ', start);
            const auto token_end = (end == std::string::npos) ? line.size() : end;
            std::int64_t value {};
            if (!parse_signed(line.substr(start, token_end - start), value))
                return {};
            if (value == 0L)
            {
                terminated = true;
                break;
            }

            const auto variable_index = static_cast<std::uint32_t>((value < 0L) ? -value : value);
            result.push_back(literal {variable {variable_index}, value < 0L});
            start = (token_end == line.size()) ? line.size() : token_end + 1u;
        }

        if (!terminated)
            return {};
        return result;
    }
}
