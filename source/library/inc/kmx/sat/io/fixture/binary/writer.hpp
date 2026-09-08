/// @file inc/kmx/sat/io/fixture/binary/writer.hpp
/// @brief Deterministic fixture generation from normalized internal input representations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <charconv>
    #include <cstdint>
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
        using literal_span_t = std::span<const kmx::sat::literal>;

        /// @brief Constructs a writer with an embedded schema and format writer.
        /// @throws None (noexcept).
        writer() noexcept = default;

        /// @brief Begins a new fixture of the given payload kind.
        /// @param kind Payload kind (`cnf_fixture` or `solve_request_fixture`) for the fixture being written.
        /// @throws None (noexcept).
        void begin_fixture(const schema::payload_kind kind) noexcept;

        /// @brief Appends one normalized clause's literals to the fixture payload.
        /// @param literals Literals composing the clause.
        /// @throws None (noexcept).
        void append_clause(const literal_span_t literals) noexcept
        {
            clauses_.emplace_back(literals.begin(), literals.end());
            update_max_variable_index(literals);
        }

        /// @brief Appends assumption literals to a `solve_request_fixture` payload.
        /// @param assumption_literals Assumption literals to append.
        /// @throws None (noexcept).
        void append_assumptions(const literal_span_t assumption_literals) noexcept;

        /// @brief Records the full solve-request limits payload for a `solve_request_fixture`.
        /// @param request Solve-request payload to serialize.
        /// @throws None (noexcept).
        void set_limits_payload(const solve_request& request) noexcept;

        /// @brief Completes the fixture payload and finalizes envelope metadata.
        /// @throws None (noexcept).
        void finalize_fixture() noexcept;

        /// @brief Computes and appends the payload checksum required by fixture validation.
        /// @throws None (noexcept).
        void write_checksum() noexcept;

        /// @brief Exposes the finalized serialized fixture bytes.
        /// @return Serialized fixture envelope and payload.
        /// @throws None (noexcept).
        std::string_view serialized_fixture() const noexcept { return serialized_fixture_; }

        /// @brief Exposes the checksum computed for the current payload.
        /// @return Current payload checksum.
        /// @throws None (noexcept).
        std::uint64_t checksum() const noexcept { return checksum_; }

    private:
        static void append_number(std::string& output, const std::uint64_t value) noexcept;

        void update_max_variable_index(const literal_span_t literals) noexcept
        {
            for (const auto lit: literals)
                max_variable_index_ = std::max(max_variable_index_, lit.variable_of().index());
        }

        io::writer::format format_ {};
        schema schema_ {};
        clause_list_t clauses_ {};
        std::vector<literal> assumptions_ {};
        solve_request request_ {};
        std::string serialized_fixture_ {};
        std::uint64_t checksum_ {};
        std::uint32_t max_variable_index_ {};
    };
}
