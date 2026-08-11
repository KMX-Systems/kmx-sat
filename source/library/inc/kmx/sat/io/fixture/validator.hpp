/// @file inc/kmx/sat/io/fixture/validator.hpp
/// @brief Structural and semantic validation before any clause insertion side effects.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
    #include <string>
    #include <string_view>
    #include <vector>
#endif
#include <kmx/sat/io/fixture/schema.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>

namespace kmx::sat::io::fixture
{
    /// @brief Structural and semantic validation before any clause insertion side effects.
    ///
    /// @details
    /// `validator` implements every mandatory binary-fixture validation rule as an isolated, side-effect-free check
    /// that `binary::reader` calls before any clause/assumption is materialized into `external_frontend`, so a
    /// rejected fixture leaves the solver state untouched (rollback-safe): `validate_magic` checks the `SATB`
    /// identifier; `validate_version` checks the schema version against `schema::supported_versions`;
    /// `validate_checksum` verifies the payload checksum; `validate_literal_domain` applies the same literal-range
    /// rules used by DIMACS/API input; `validate_clause_shapes` rejects malformed/empty clause records; and
    /// `validate_limits_payload` applies the same range checks as runtime API input to a `solve_request_fixture`'s
    /// assumptions and limits.
    /// @note In strict mode, any failure here must map to `parse_error`, `domain_error`, or `consistency_error`
    /// immediately; in tolerant mode, only benign metadata irregularities may be tolerated, and literal/clause
    /// semantic corruption must never be tolerated regardless of mode.
    class validator final
    {
    public:
        using clause_container = std::vector<std::vector<literal>>;

        /// @brief Constructs a validator with no cached fixture state.
        /// @throws None (noexcept).
        validator() noexcept = default;

        /// @brief Records the parsed fixture envelope header for later validation.
        /// @param magic Envelope magic identifier.
        /// @param version Parsed schema version.
        /// @param declared_checksum Envelope-declared checksum.
        /// @param fixture_schema Schema metadata associated with the fixture.
        /// @throws None (noexcept).
        void set_header(const std::string_view magic, const std::uint16_t version, const std::uint64_t declared_checksum,
                        const schema& fixture_schema) noexcept
        {
            magic_.assign(magic.begin(), magic.end());
            version_ = version;
            declared_checksum_ = declared_checksum;
            schema_ = fixture_schema;
        }

        /// @brief Records the declared/effective variable bound that payload literals must satisfy.
        /// @param variable_count Maximum allowed variable index.
        /// @throws None (noexcept).
        void set_declared_variable_count(const std::uint32_t variable_count) noexcept
        {
            declared_variable_count_ = variable_count;
        }

        /// @brief Records the clause payload to be validated.
        /// @param clauses Clause records decoded from the fixture payload.
        /// @throws None (noexcept).
        void set_clauses(const clause_container& clauses) noexcept
        {
            clauses_ = clauses;
        }

        /// @brief Records the assumption payload to be validated.
        /// @param assumptions Assumption literals decoded from the fixture payload.
        /// @throws None (noexcept).
        void set_assumptions(const std::span<const literal> assumptions) noexcept
        {
            assumptions_.assign(assumptions.begin(), assumptions.end());
        }

        /// @brief Records the solve-request-style limits payload to be validated.
        /// @param request Limits payload decoded from the fixture.
        /// @throws None (noexcept).
        void set_limits_payload(const solve_request& request) noexcept
        {
            request_ = request;
        }

        /// @brief Computes the checksum implied by the currently recorded payload state.
        /// @return Deterministic checksum over the recorded payload fields.
        /// @throws None (noexcept).
        [[nodiscard]] std::uint64_t payload_checksum() const noexcept
        {
            std::uint64_t checksum {1469598103934665603ull};
            const auto mix = [&checksum](const std::uint64_t value) noexcept {
                checksum ^= value;
                checksum *= 1099511628211ull;
            };

            mix(version_);
            mix(static_cast<std::uint64_t>(schema_.payload_kind_of()));
            mix(declared_variable_count_);
            mix(schema_.feature_flags());

            for (const auto& clause : clauses_)
            {
                mix(clause.size());
                for (const auto lit : clause)
                {
                    mix(lit.raw());
                }
            }
            for (const auto lit : assumptions_)
            {
                mix(lit.raw());
            }
            for (const auto lit : request_.assumptions)
            {
                mix(lit.raw());
            }
            mix(request_.conflict_limit);
            mix(request_.decision_limit);
            mix(request_.enabled_pass_mask);
            mix(static_cast<std::uint64_t>(request_.strict_mode));
            return checksum;
        }

        /// @brief Validates that the fixture envelope's magic identifier matches `SATB`.
        /// @return True if the magic identifier is correct.
        /// @throws None (noexcept).
        bool validate_magic() const noexcept
        {
            return magic_ == "SATB";
        }

        /// @brief Validates that the fixture's schema version is supported by this build.
        /// @return True if the schema version is supported.
        /// @throws None (noexcept).
        bool validate_version() const noexcept
        {
            return schema_.supported_versions(version_) && schema_.endian_policy() && schema_.integer_width_policy() &&
                   (schema_.feature_flags() & ~schema::supported_feature_mask) == 0u;
        }

        /// @brief Validates the fixture payload's checksum against the envelope-declared value.
        /// @return True if the checksum matches.
        /// @throws None (noexcept).
        bool validate_checksum() const noexcept
        {
            return declared_checksum_ == payload_checksum();
        }

        /// @brief Validates that every literal in the payload falls within the declared/effective variable domain.
        /// @return True if every literal is within domain.
        /// @throws None (noexcept).
        bool validate_literal_domain() const noexcept
        {
            const auto in_domain = [this](const literal lit) noexcept {
                const auto index = lit.variable_of().index();
                return index != 0u && index <= declared_variable_count_;
            };

            for (const auto& clause : clauses_)
            {
                for (const auto lit : clause)
                {
                    if (!in_domain(lit))
                    {
                        return false;
                    }
                }
            }
            for (const auto lit : assumptions_)
            {
                if (!in_domain(lit))
                {
                    return false;
                }
            }
            for (const auto lit : request_.assumptions)
            {
                if (!in_domain(lit))
                {
                    return false;
                }
            }
            return true;
        }

        /// @brief Validates that clause records are well-formed (no malformed or empty records where disallowed).
        /// @return True if every clause shape is valid.
        /// @throws None (noexcept).
        bool validate_clause_shapes() const noexcept
        {
            if (schema_.payload_kind_of() != schema::payload_kind::cnf_fixture)
            {
                return true;
            }

            for (const auto& clause : clauses_)
            {
                if (clause.empty())
                {
                    return false;
                }
                for (const auto lit : clause)
                {
                    if (lit.raw() == 0u)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        /// @brief Validates that a `solve_request_fixture` payload's assumptions and limits pass runtime-input rules.
        /// @return True if the limits payload is valid.
        /// @throws None (noexcept).
        bool validate_limits_payload() const noexcept
        {
            if (schema_.payload_kind_of() != schema::payload_kind::solve_request_fixture)
            {
                return true;
            }

            for (const auto lit : request_.assumptions)
            {
                if (lit.raw() == 0u)
                {
                    return false;
                }
            }
            return validate_literal_domain();
        }

    private:
        schema schema_ {};
        std::string magic_ {};
        std::uint16_t version_ {};
        std::uint64_t declared_checksum_ {};
        std::uint32_t declared_variable_count_ {};
        clause_container clauses_ {};
        std::vector<literal> assumptions_ {};
        solve_request request_ {};
    };
}
