/// @file inc/kmx/sat/io/fixture/binary/reader.hpp
/// @brief Fast binary ingestion path for tests, replay, and benchmarks; never replaces the canonical DIMACS path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
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
    /// @details
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
        bool open_fixture(file_source& source) noexcept;

        /// @brief Reads the fixture envelope header (magic, version, endianness, feature flags, payload metadata).
        /// @return True if the header was read successfully.
        /// @throws None (noexcept).
        bool read_header() noexcept;

        /// @brief Validates the header against the current build's supported schema/feature set.
        /// @return True if the header is structurally and semantically valid.
        /// @throws None (noexcept).
        bool validate_header() const noexcept { return validator_.validate_magic() && validator_.validate_version(); }

        /// @brief Loads the fixture payload described by the validated header.
        /// @return True if the payload was loaded successfully.
        /// @throws None (noexcept).
        bool load_payload() noexcept;

        /// @brief Validates the loaded payload's checksum, literal domain, and clause/limits shapes.
        /// @return True if the payload passes every validation rule.
        /// @throws None (noexcept).
        bool validate_payload() const noexcept
        {
            return (clauses_.size() == clause_count_) && (assumptions_.size() == assumption_count_) && validator_.validate_checksum() &&
                   validator_.validate_literal_domain() && validator_.validate_clause_shapes() && validator_.validate_limits_payload();
        }

        /// @brief Materializes the validated fixture data into the external frontend, applying normal solver checks.
        /// @param frontend External frontend to receive the materialized clauses/assumptions/limits.
        /// @throws None (noexcept).
        void materialize_fixture_into_frontend(cdcl::external_frontend& frontend) noexcept;

        /// @brief Closes the fixture source, releasing its resources.
        /// @throws None (noexcept).
        void close_fixture() noexcept;

        /// @brief Exposes the loaded clause payload after `load_payload`.
        /// @return Read-only view over loaded clause records.
        /// @throws None (noexcept).
        clause_span_t clauses() const noexcept { return clauses_; }

        /// @brief Exposes the loaded assumptions payload after `load_payload`.
        /// @return Read-only view over loaded assumption literals.
        /// @throws None (noexcept).
        std::span<const literal> assumptions() const noexcept { return assumptions_; }

        /// @brief Exposes the loaded solve-request limits payload after `load_payload`.
        /// @return Loaded solve-request payload.
        /// @throws None (noexcept).
        const solve_request& request_payload() const noexcept { return request_; }

    private:
        static std::string_view trim(std::string_view input) noexcept;

        static bool parse_unsigned(std::string_view token, std::uint64_t& value) noexcept;

        static bool parse_signed(std::string_view token, std::int64_t& value) noexcept;

        bool parse_header_line(const std::string_view header) noexcept;

        static optional_clause_t parse_clause_line(const std::string_view line) noexcept;

        validator validator_ {};
        file_source* source_ {};
        schema schema_ {};
        std::string raw_fixture_ {};
        clause_list_t clauses_ {};
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
