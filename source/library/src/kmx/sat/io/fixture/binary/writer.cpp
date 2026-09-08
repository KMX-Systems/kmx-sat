/// @file library/src/kmx/sat/io/fixture/binary/writer.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/fixture/binary/writer.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/fixture/binary/writer.hpp>

namespace kmx::sat::io::fixture::binary
{
    void writer::begin_fixture(const schema::payload_kind kind) noexcept
    {
        schema_ = schema {};
        schema_.set_payload_kind(kind);
        clauses_.clear();
        assumptions_.clear();
        request_ = solve_request {};
        serialized_fixture_.clear();
        format_.clear();
        checksum_ = 0u;
        max_variable_index_ = 0u;
    }

    void writer::append_assumptions(const literal_span_t assumption_literals) noexcept
    {
        assumptions_.insert(assumptions_.end(), assumption_literals.begin(), assumption_literals.end());
        request_.assumptions = assumptions_;
        update_max_variable_index(assumption_literals);
    }

    void writer::set_limits_payload(const solve_request& request) noexcept
    {
        request_ = request;
        assumptions_ = request.assumptions;
        max_variable_index_ = 0u;
        for (const auto& clause: clauses_)
            update_max_variable_index(clause);
        update_max_variable_index(assumptions_);
    }

    void writer::finalize_fixture() noexcept
    {
        if (checksum_ == 0u)
            write_checksum();

        format_.clear();
        std::string header {};
        header.reserve(128u);
        header.append("SATB ");
        append_number(header, schema_.current_version());
        header.push_back(' ');
        append_number(header, static_cast<std::uint64_t>(schema_.payload_kind_of()));
        header.push_back(' ');
        append_number(header, schema_.feature_flags());
        header.push_back(' ');
        append_number(header, max_variable_index_);
        header.push_back(' ');
        append_number(header, clauses_.size());
        header.push_back(' ');
        append_number(header, assumptions_.size());
        header.push_back(' ');
        append_number(header, request_.conflict_limit);
        header.push_back(' ');
        append_number(header, request_.decision_limit);
        header.push_back(' ');
        append_number(header, request_.enabled_pass_mask);
        header.push_back(' ');
        append_number(header, request_.strict_mode ? 1u : 0u);
        header.push_back(' ');
        append_number(header, checksum_);
        format_.write_report_line(header);

        for (const auto& clause: clauses_)
        {
            format_.write_report_line('c');
            format_.write_clause(clause);
        }

        if (schema_.payload_kind_of() == schema::payload_kind::solve_request_fixture)
        {
            format_.write_report_line('a');
            format_.write_clause(assumptions_);
        }

        serialized_fixture_.assign(format_.buffer_view().begin(), format_.buffer_view().end());
    }

    void writer::write_checksum() noexcept
    {
        validator fixture_validator {};
        fixture_validator.set_declared_variable_count(max_variable_index_);
        fixture_validator.set_clauses(clauses_);
        fixture_validator.set_assumptions(assumptions_);
        fixture_validator.set_limits_payload(request_);
        fixture_validator.set_header("SATB", schema_.current_version(), 0u, schema_);
        checksum_ = fixture_validator.payload_checksum();
    }

    void writer::append_number(std::string& output, const std::uint64_t value) noexcept
    {
        std::array<char, 32u> number {};
        const auto result = std::to_chars(number.data(), number.data() + number.size(), value);
        output.append(number.data(), static_cast<std::size_t>(result.ptr - number.data()));
    }
}
