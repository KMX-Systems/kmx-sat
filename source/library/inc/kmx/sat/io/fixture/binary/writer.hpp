/// @file inc/kmx/sat/io/fixture/binary/writer.hpp
/// @brief Deterministic fixture generation from normalized internal input representations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
    #include <string>
    #include <vector>
#endif
#include <kmx/sat/io/fixture/schema.hpp>
#include <kmx/sat/io/fixture/validator.hpp>
#include <kmx/sat/io/writer/format.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>

namespace kmx::sat::io::fixture::binary
{
    /// @brief Deterministic fixture generation from normalized internal input representations.
    ///
    /// @details
    /// `writer` is the counterpart to `binary::reader`: it produces the `SATB`-tagged binary envelope defined by
    /// `schema` from already-normalized in-memory data rather than from live solver state, ensuring fixtures are
    /// reproducible byte-for-byte given the same input. `begin_fixture` opens a new fixture of a given
    /// `schema::payload_kind` (`cnf_fixture` or `solve_request_fixture`); `append_clause`/`append_assumptions` add
    /// normalized clause vectors or assumption literals to the payload; `finalize_fixture` completes the payload and
    /// writes envelope metadata (length, feature flags); `write_checksum` computes and appends the payload checksum
    /// required by fixture validation.
    class writer final
    {
    public:
        using literal_span = std::span<const kmx::sat::literal>;

        /// @brief Constructs a writer with an embedded schema and format writer.
        /// @throws None (noexcept).
        writer() noexcept = default;

        /// @brief Begins a new fixture of the given payload kind.
        /// @param kind Payload kind (`cnf_fixture` or `solve_request_fixture`) for the fixture being written.
        /// @throws None (noexcept).
        void begin_fixture(const schema::payload_kind kind) noexcept
        {
            schema_ = schema {};
            schema_.set_payload_kind(kind);
            clauses_.clear();
            assumptions_.clear();
            request_ = solve_request {};
            serialized_fixture_.clear();
            format_.clear();
            checksum_ = 0u;
        }

        /// @brief Appends one normalized clause's literals to the fixture payload.
        /// @param literals Literals composing the clause.
        /// @throws None (noexcept).
        void append_clause(const literal_span literals) noexcept { clauses_.emplace_back(literals.begin(), literals.end()); }

        /// @brief Appends assumption literals to a `solve_request_fixture` payload.
        /// @param assumption_literals Assumption literals to append.
        /// @throws None (noexcept).
        void append_assumptions(const literal_span assumption_literals) noexcept
        {
            assumptions_.insert(assumptions_.end(), assumption_literals.begin(), assumption_literals.end());
            request_.assumptions = assumptions_;
        }

        /// @brief Records the full solve-request limits payload for a `solve_request_fixture`.
        /// @param request Solve-request payload to serialize.
        /// @throws None (noexcept).
        void set_limits_payload(const solve_request& request) noexcept
        {
            request_ = request;
            assumptions_ = request.assumptions;
        }

        /// @brief Completes the fixture payload and finalizes envelope metadata.
        /// @throws None (noexcept).
        void finalize_fixture() noexcept
        {
            if (checksum_ == 0u)
            {
                write_checksum();
            }

            format_.clear();
            format_.write_report_line("SATB " + std::to_string(schema_.current_version()) + " " +
                                      std::to_string(static_cast<int>(schema_.payload_kind_of())) + " " +
                                      std::to_string(schema_.feature_flags()) + " " + std::to_string(max_variable_index()) + " " +
                                      std::to_string(clauses_.size()) + " " + std::to_string(assumptions_.size()) + " " +
                                      std::to_string(request_.conflict_limit) + " " + std::to_string(request_.decision_limit) + " " +
                                      std::to_string(request_.enabled_pass_mask) + " " + std::to_string(request_.strict_mode ? 1 : 0) +
                                      " " + std::to_string(checksum_));

            for (const auto& clause: clauses_)
            {
                format_.write_report_line("c");
                format_.write_clause(clause);
            }

            if (schema_.payload_kind_of() == schema::payload_kind::solve_request_fixture)
            {
                format_.write_report_line("a");
                format_.write_clause(assumptions_);
            }

            serialized_fixture_.assign(format_.buffer_view().begin(), format_.buffer_view().end());
        }

        /// @brief Computes and appends the payload checksum required by fixture validation.
        /// @throws None (noexcept).
        void write_checksum() noexcept
        {
            validator fixture_validator {};
            fixture_validator.set_declared_variable_count(max_variable_index());
            fixture_validator.set_clauses(clauses_);
            fixture_validator.set_assumptions(assumptions_);
            fixture_validator.set_limits_payload(request_);
            fixture_validator.set_header("SATB", schema_.current_version(), 0u, schema_);
            checksum_ = fixture_validator.payload_checksum();
        }

        /// @brief Exposes the finalized serialized fixture bytes.
        /// @return Serialized fixture envelope and payload.
        /// @throws None (noexcept).
        std::string_view serialized_fixture() const noexcept { return serialized_fixture_; }

        /// @brief Exposes the checksum computed for the current payload.
        /// @return Current payload checksum.
        /// @throws None (noexcept).
        std::uint64_t checksum() const noexcept { return checksum_; }

    private:
        std::uint32_t max_variable_index() const noexcept
        {
            std::uint32_t max_index {};
            const auto update = [&max_index](const literal lit) noexcept
            {
                if (lit.variable_of().index() > max_index)
                {
                    max_index = lit.variable_of().index();
                }
            };

            for (const auto& clause: clauses_)
            {
                for (const auto lit: clause)
                {
                    update(lit);
                }
            }
            for (const auto lit: assumptions_)
            {
                update(lit);
            }
            return max_index;
        }

        io::writer::format format_ {};
        schema schema_ {};
        std::vector<std::vector<literal>> clauses_ {};
        std::vector<literal> assumptions_ {};
        solve_request request_ {};
        std::string serialized_fixture_ {};
        std::uint64_t checksum_ {};
    };
}
