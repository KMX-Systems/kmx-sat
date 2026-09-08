/// @file library/src/kmx/sat/io/fixture/validator.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/fixture/validator.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/fixture/validator.hpp>

namespace kmx::sat::io::fixture
{
    void validator::set_header(const std::string_view magic, const std::uint16_t version, const std::uint64_t declared_checksum,
                               const schema& fixture_schema) noexcept
    {
        magic_.assign(magic.begin(), magic.end());
        version_ = version;
        declared_checksum_ = declared_checksum;
        schema_ = fixture_schema;
    }

    [[nodiscard]] std::uint64_t validator::payload_checksum() const noexcept
    {
        std::uint64_t checksum {1469598103934665603ull};
        const auto mix = [&checksum](const std::uint64_t value) noexcept
        {
            checksum ^= value;
            checksum *= 1099511628211ull;
        };

        mix(version_);
        mix(static_cast<std::uint64_t>(schema_.payload_kind_of()));
        mix(declared_variable_count_);
        mix(schema_.feature_flags());

        for (const auto& clause: clauses_)
        {
            mix(clause.size());
            for (const auto lit: clause)
                mix(lit.raw());
        }
        for (const auto lit: assumptions_)
            mix(lit.raw());
        for (const auto lit: request_.assumptions)
            mix(lit.raw());
        mix(request_.conflict_limit);
        mix(request_.decision_limit);
        mix(request_.enabled_pass_mask);
        mix(static_cast<std::uint64_t>(request_.strict_mode));
        return checksum;
    }

    bool validator::validate_literal_domain() const noexcept
    {
        const auto in_domain = [this](const literal lit) noexcept
        {
            const auto index = lit.variable_of().index();
            return (index != 0u) && (index <= declared_variable_count_);
        };

        for (const auto& clause: clauses_)
            for (const auto lit: clause)
                if (!in_domain(lit))
                    return false;
        for (const auto lit: assumptions_)
            if (!in_domain(lit))
                return false;
        for (const auto lit: request_.assumptions)
            if (!in_domain(lit))
                return false;
        return true;
    }

    bool validator::validate_clause_shapes() const noexcept
    {
        if (schema_.payload_kind_of() != schema::payload_kind::cnf_fixture)
            return true;

        for (const auto& clause: clauses_)
        {
            if (clause.empty())
                return false;
            for (const auto lit: clause)
                if (lit.raw() == 0u)
                    return false;
        }
        return true;
    }

    bool validator::validate_limits_payload() const noexcept
    {
        if (schema_.payload_kind_of() != schema::payload_kind::solve_request_fixture)
            return true;

        for (const auto lit: request_.assumptions)
            if (lit.raw() == 0u)
                return false;
        return validate_literal_domain();
    }
}
