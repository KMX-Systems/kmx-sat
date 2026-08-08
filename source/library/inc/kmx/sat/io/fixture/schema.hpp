/// @file inc/kmx/sat/io/fixture/schema.hpp
/// @brief Versioned schema contract for binary fixtures and compatibility checks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::io::fixture
{
    /// @brief Versioned schema contract for binary fixtures and compatibility checks.
    ///
    /// `schema` centralizes every piece of metadata `binary::reader`/`binary::writer` need to agree on: the current
    /// schema version this build writes (`current_version`) and which versions it can still read
    /// (`supported_versions`), the payload kind in use (`payload_kind_of`, distinguishing `cnf_fixture` from
    /// `solve_request_fixture`), the `feature_flags` bitset gating optional payload features, and the
    /// endianness/integer-width policy (`endian_policy`/`integer_width_policy`) needed to safely convert or reject
    /// fixtures written on a different architecture. This is pure metadata/validation-helper state with no I/O of
    /// its own.
    class schema final
    {
    public:
        /// @brief Enumerates the recognized binary fixture payload kinds.
        enum class payload_kind
        {
            /// @brief Payload holding a normalized CNF formula (declared variable/clause counts plus clause vectors).
            cnf_fixture,
            /// @brief Payload holding a replayable solve request (assumptions and limits).
            solve_request_fixture
        };

        /// @brief Constructs a schema instance describing this build's current fixture version/policies.
        /// @throws None (noexcept).
        schema() noexcept = default;

        /// @brief Returns the schema version this build writes for new fixtures.
        /// @return Current schema version number.
        /// @throws None (noexcept).
        std::uint16_t current_version() const noexcept
        {
            return {};
        }

        /// @brief Checks whether a given schema version can still be read by this build.
        /// @param version Schema version to check.
        /// @return True if `version` is supported for reading.
        /// @throws None (noexcept).
        bool supported_versions(const std::uint16_t version) const noexcept
        {
            return false;
        }

        /// @brief Returns the payload kind currently associated with this schema instance.
        /// @return Payload kind value.
        /// @throws None (noexcept).
        payload_kind payload_kind_of() const noexcept
        {
            return payload_kind::cnf_fixture;
        }

        /// @brief Returns the feature-flags bitset gating optional payload features.
        /// @return Feature flags bitset.
        /// @throws None (noexcept).
        std::uint64_t feature_flags() const noexcept
        {
            return {};
        }

        /// @brief Checks whether the fixture's endianness is compatible with (or safely convertible to) this build.
        /// @return True if endianness is compatible or convertible.
        /// @throws None (noexcept).
        bool endian_policy() const noexcept
        {
            return false;
        }

        /// @brief Checks whether the fixture's integer width is compatible with (or safely convertible to) this build.
        /// @return True if integer width is compatible or convertible.
        /// @throws None (noexcept).
        bool integer_width_policy() const noexcept
        {
            return false;
        }
    };
}
