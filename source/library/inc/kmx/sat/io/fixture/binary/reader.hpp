/// @file inc/kmx/sat/io/fixture/binary/reader.hpp
/// @brief Fast binary ingestion path for tests, replay, and benchmarks; never replaces the canonical DIMACS path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <charconv>
    #include <cstdint>
    #include <optional>
    #include <span>
    #include <string>
    #include <string_view>
    #include <vector>
#endif
#include <kmx/sat/cdcl/external_frontend.hpp>
#include <kmx/sat/io/file_source.hpp>
#include <kmx/sat/io/fixture/schema.hpp>
#include <kmx/sat/io/fixture/validator.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/telemetry/solver_options.hpp>

namespace kmx::sat::io::fixture::binary
{
    /// @brief Fast binary ingestion path for tests, replay, and benchmarks; never replaces the canonical DIMACS path.
    ///
    /// The binary fixture channel is a feature-gated shortcut around DIMACS's text parsing overhead for CI/benchmark
    /// corpora, following the envelope defined by `schema`: `open_fixture` reads the `SATB`-tagged envelope from a
    /// `file_source`; `read_header`/`validate_header` parse and check the magic/version/endianness/feature-flag
    /// header before any payload is touched; `load_payload`/`validate_payload` decode and validate the `cnf_fixture`
    /// or `solve_request_fixture` body (delegating structural/semantic checks to the owned `validator`);
    /// `materialize_fixture_into_frontend` feeds the validated data through `external_frontend` using the exact same
    /// normalization and state-machine legality checks as DIMACS/API input, so DIMACS and fixture ingestion are
    /// required to produce equivalent normalized clause sets for the same source formula. `close_fixture` releases
    /// the underlying source.
    /// @warning This reader must only ever deserialize stable value data (literals, clause vectors, assumptions,
    /// limits, metadata); it must never deserialize live arena offsets, watcher entries, reason references, or
    /// transient trail internals. On any validation failure, materialization must be rollback-safe with no partial
    /// clause insertion.
    class reader final
    {
    public:
        /// @brief Constructs a reader with an embedded validator and no open fixture.
        /// @throws None (noexcept).
        reader() noexcept = default;

        /// @brief Opens a binary fixture source for reading.
        /// @param source File source containing the fixture envelope and payload.
        /// @return True if the source was opened successfully.
        /// @throws None (noexcept).
        bool open_fixture(file_source& source) noexcept
        {
            close_fixture();
            source_ = &source;
            return true;
        }

        /// @brief Reads the fixture envelope header (magic, version, endianness, feature flags, payload metadata).
        /// @return True if the header was read successfully.
        /// @throws None (noexcept).
        bool read_header() noexcept
        {
            if (source_ == nullptr)
                return false;

            raw_fixture_.clear();
            char buffer[4096] {};
            for (;;)
            {
                const auto read_count = source_->read(buffer, sizeof(buffer));
                if (read_count == 0)
                    break;
                raw_fixture_.append(buffer, read_count);
            }

            if (raw_fixture_.empty())
                return false;

            const auto newline = raw_fixture_.find('\n');
            if (newline == std::string::npos)
                return false;

            std::string_view header {raw_fixture_.data(), newline};
            return parse_header_line(header);
        }

        /// @brief Validates the header against the current build's supported schema/feature set.
        /// @return True if the header is structurally and semantically valid.
        /// @throws None (noexcept).
        bool validate_header() const noexcept { return validator_.validate_magic() && validator_.validate_version(); }

        /// @brief Loads the fixture payload described by the validated header.
        /// @return True if the payload was loaded successfully.
        /// @throws None (noexcept).
        bool load_payload() noexcept
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
            std::size_t line_start = newline == std::string::npos ? raw_fixture_.size() : newline + 1u;
            while (line_start < raw_fixture_.size())
            {
                const auto line_end = raw_fixture_.find('\n', line_start);
                const auto line_length = line_end == std::string::npos ? raw_fixture_.size() - line_start : line_end - line_start;
                std::string_view line {raw_fixture_.data() + line_start, line_length};
                line_start = line_end == std::string::npos ? raw_fixture_.size() : line_end + 1u;

                if (line.empty())
                    continue;

                if (line == "c")
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

                if (line == "a")
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
            }

            if (parsed_clause_count != clause_count_ || parsed_assumption_count != assumption_count_)
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

        /// @brief Validates the loaded payload's checksum, literal domain, and clause/limits shapes.
        /// @return True if the payload passes every validation rule.
        /// @throws None (noexcept).
        bool validate_payload() const noexcept
        {
            return clauses_.size() == clause_count_ && assumptions_.size() == assumption_count_ && validator_.validate_checksum() &&
                   validator_.validate_literal_domain() && validator_.validate_clause_shapes() && validator_.validate_limits_payload();
        }

        /// @brief Materializes the validated fixture data into the external frontend, applying normal solver checks.
        /// @param frontend External frontend to receive the materialized clauses/assumptions/limits.
        /// @throws None (noexcept).
        void materialize_fixture_into_frontend(cdcl::external_frontend& frontend) noexcept
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

        /// @brief Closes the fixture source, releasing its resources.
        /// @throws None (noexcept).
        void close_fixture() noexcept
        {
            source_ = nullptr;
            raw_fixture_.clear();
            clauses_.clear();
            assumptions_.clear();
            request_ = solve_request {};
        }

        /// @brief Exposes the loaded clause payload after `load_payload`.
        /// @return Read-only view over loaded clause records.
        /// @throws None (noexcept).
        std::span<const std::vector<literal>> clauses() const noexcept { return clauses_; }

        /// @brief Exposes the loaded assumptions payload after `load_payload`.
        /// @return Read-only view over loaded assumption literals.
        /// @throws None (noexcept).
        std::span<const literal> assumptions() const noexcept { return assumptions_; }

        /// @brief Exposes the loaded solve-request limits payload after `load_payload`.
        /// @return Loaded solve-request payload.
        /// @throws None (noexcept).
        const solve_request& request_payload() const noexcept { return request_; }

    private:
        static std::string_view trim(std::string_view input) noexcept
        {
            while (!input.empty() && (input.front() == ' ' || input.front() == '\t' || input.front() == '\r'))
                input.remove_prefix(1);
            while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r'))
                input.remove_suffix(1);
            return input;
        }

        static bool parse_unsigned(std::string_view token, std::uint64_t& value) noexcept
        {
            token = trim(token);
            if (token.empty())
                return false;

            std::uint64_t parsed {};
            const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
            if (result.ec != std::errc {} || result.ptr != token.data() + token.size())
                return false;
            value = parsed;
            return true;
        }

        static bool parse_signed(std::string_view token, std::int64_t& value) noexcept
        {
            token = trim(token);
            if (token.empty())
                return false;

            std::int64_t parsed {};
            const auto result = std::from_chars(token.data(), token.data() + token.size(), parsed);
            if (result.ec != std::errc {} || result.ptr != token.data() + token.size())
                return false;
            value = parsed;
            return true;
        }

        bool parse_header_line(const std::string_view header) noexcept
        {
            std::vector<std::string_view> tokens {};
            std::size_t start {};
            while (start < header.size())
            {
                while (start < header.size() && header[start] == ' ')
                    ++start;
                if (start >= header.size())
                    break;
                const auto end = header.find(' ', start);
                const auto token_end = end == std::string::npos ? header.size() : end;
                tokens.push_back(header.substr(start, token_end - start));
                start = token_end == header.size() ? header.size() : token_end + 1u;
            }

            if (tokens.size() != 12u)
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

            if (tokens[0] != "SATB" || !parse_unsigned(tokens[1], parsed_version) || !parse_unsigned(tokens[2], kind_value) ||
                !parse_unsigned(tokens[3], flags_value) || !parse_unsigned(tokens[4], declared_vars) ||
                !parse_unsigned(tokens[5], parsed_clause_count) || !parse_unsigned(tokens[6], parsed_assumption_count) ||
                !parse_unsigned(tokens[7], parsed_conflict_limit) || !parse_unsigned(tokens[8], parsed_decision_limit) ||
                !parse_unsigned(tokens[9], parsed_pass_mask) || !parse_unsigned(tokens[10], parsed_strict_mode) ||
                !parse_unsigned(tokens[11], parsed_checksum))
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
            validator_.set_header(tokens[0], version_, declared_checksum_, schema_);
            return true;
        }

        static std::optional<std::vector<literal>> parse_clause_line(const std::string_view line) noexcept
        {
            std::vector<literal> result {};
            std::size_t start {};
            bool terminated {};
            while (start < line.size())
            {
                while (start < line.size() && line[start] == ' ')
                    ++start;
                if (start >= line.size())
                    break;
                const auto end = line.find(' ', start);
                const auto token_end = end == std::string::npos ? line.size() : end;
                std::int64_t value {};
                if (!parse_signed(line.substr(start, token_end - start), value))
                    return {};
                if (value == 0)
                {
                    terminated = true;
                    break;
                }

                const auto variable_index = static_cast<std::uint32_t>(value < 0 ? -value : value);
                result.push_back(literal {variable {variable_index}, value < 0});
                start = token_end == line.size() ? line.size() : token_end + 1u;
            }

            if (!terminated)
                return {};
            return result;
        }

        validator validator_ {};
        file_source* source_ {};
        schema schema_ {};
        std::string raw_fixture_ {};
        std::vector<std::vector<literal>> clauses_ {};
        std::vector<literal> assumptions_ {};
        solve_request request_ {};
        std::uint16_t version_ {};
        std::uint32_t declared_variable_count_ {};
        std::size_t clause_count_ {};
        std::size_t assumption_count_ {};
        std::uint64_t conflict_limit_ {};
        std::uint64_t decision_limit_ {};
        std::uint64_t enabled_pass_mask_ {};
        bool strict_mode_ {};
        std::uint64_t declared_checksum_ {};
    };
}
