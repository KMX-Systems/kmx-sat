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
    /// @details
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
        static constexpr std::uint16_t current_version_value {1};
        static constexpr std::uint64_t supported_feature_mask {};

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

        /// @brief Selects the payload kind associated with this schema instance.
        /// @param kind Payload kind to record.
        /// @throws None (noexcept).
        void set_payload_kind(const payload_kind kind) noexcept { payload_kind_ = kind; }

        /// @brief Records the feature-flag bitset associated with this schema instance.
        /// @param flags Feature flags to record.
        /// @throws None (noexcept).
        void set_feature_flags(const std::uint64_t flags) noexcept { feature_flags_ = flags; }

        /// @brief Records whether the fixture endianness is compatible with this build.
        /// @param compatible True if endianness is compatible or convertible.
        /// @throws None (noexcept).
        void set_endian_compatible(const bool compatible) noexcept { endian_compatible_ = compatible; }

        /// @brief Records whether the fixture integer width is compatible with this build.
        /// @param compatible True if integer width is compatible or convertible.
        /// @throws None (noexcept).
        void set_integer_width_compatible(const bool compatible) noexcept { integer_width_compatible_ = compatible; }

        /// @brief Returns the schema version this build writes for new fixtures.
        /// @return Current schema version number.
        /// @throws None (noexcept).
        std::uint16_t current_version() const noexcept { return current_version_value; }

        /// @brief Checks whether a given schema version can still be read by this build.
        /// @param version Schema version to check.
        /// @return True if `version` is supported for reading.
        /// @throws None (noexcept).
        bool supported_versions(const std::uint16_t version) const noexcept { return version == current_version_value; }

        /// @brief Returns the payload kind currently associated with this schema instance.
        /// @return Payload kind value.
        /// @throws None (noexcept).
        payload_kind payload_kind_of() const noexcept { return payload_kind_; }

        /// @brief Returns the feature-flags bitset gating optional payload features.
        /// @return Feature flags bitset.
        /// @throws None (noexcept).
        std::uint64_t feature_flags() const noexcept { return feature_flags_; }

        /// @brief Checks whether the fixture's endianness is compatible with (or safely convertible to) this build.
        /// @return True if endianness is compatible or convertible.
        /// @throws None (noexcept).
        bool endian_policy() const noexcept { return endian_compatible_; }

        /// @brief Checks whether the fixture's integer width is compatible with (or safely convertible to) this build.
        /// @return True if integer width is compatible or convertible.
        /// @throws None (noexcept).
        bool integer_width_policy() const noexcept { return integer_width_compatible_; }

    private:
        payload_kind payload_kind_ {payload_kind::cnf_fixture};
        std::uint64_t feature_flags_ {supported_feature_mask};
        bool endian_compatible_ {true};
        bool integer_width_compatible_ {true};
    };
}
