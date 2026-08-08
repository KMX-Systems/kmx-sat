/// @file api/kmx/sat/telemetry/solver_options.hpp
/// @brief Solver options and configuration profiles.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <expected>
    #include <string_view>
#endif

namespace kmx::sat::telemetry
{
    /// @brief Solver options and configuration profiles.
    ///
    /// `solver_options` is the single reflection-ready store for every tunable value across the solver (restart
    /// intervals, EVSIDS decay, enabled simplification passes, memory ceilings, and so on): `set`/`get` provide
    /// direct name/value access, `load_profile` applies a predefined bundle of values in one call (for example a
    /// competition-mode profile versus a library-embedding profile), and `validate` checks the whole option set for
    /// range violations, unknown names, features unsupported by the current build, and inter-option dependency
    /// conflicts, reported through `std::expected` rather than exceptions since option configuration is an
    /// expected-to-sometimes-fail operation rather than a programming error. `iterate_descriptors` (declared in the
    /// plan for future Reflection-based descriptor generation) exposes the option set's metadata.
    /// @note This is one of the minimum public interfaces required to stabilize early and follows semantic
    /// versioning as part of the stable C++ surface.
    class solver_options final
    {
    public:
        /// @brief Enumerates the ways an option value or profile can fail validation.
        enum class validation_error
        {
            /// @brief A numeric option value falls outside its permitted range.
            out_of_range,
            /// @brief The requested option name is not recognized.
            unknown_option,
            /// @brief The requested option is feature-gated and not available in the current build.
            unsupported_in_baseline_build,
            /// @brief The requested option value conflicts with another currently set option.
            dependency_conflict
        };

        /// @brief Constructs an options set with implementation-defined defaults.
        /// @throws None (noexcept).
        solver_options() noexcept = default;

        /// @brief Sets one numeric option by name, without immediate cross-option validation.
        /// @param name Option identifier.
        /// @param value Option value to assign.
        /// @throws None (noexcept).
        void set(const std::string_view name, const std::int64_t value) noexcept
        {
        }

        /// @brief Returns the current value of one option by name.
        /// @param name Option identifier.
        /// @return Current option value.
        /// @throws None (noexcept).
        std::int64_t get(const std::string_view name) const noexcept
        {
            return {};
        }

        /// @brief Applies a predefined bundle of option values identified by profile name.
        /// @param profile_name Name of the profile to load.
        /// @return Success, or a `validation_error` describing why the profile could not be applied.
        /// @throws None (noexcept).
        std::expected<void, validation_error> load_profile(const std::string_view profile_name) noexcept
        {
            return {};
        }

        /// @brief Validates the entire current option set for range, dependency, and feature-availability issues.
        /// @return Success, or the first `validation_error` encountered.
        /// @throws None (noexcept).
        std::expected<void, validation_error> validate() const noexcept
        {
            return {};
        }
    };
}
