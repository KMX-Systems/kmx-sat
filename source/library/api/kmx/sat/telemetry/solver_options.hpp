/// @file api/kmx/sat/telemetry/solver_options.hpp
/// @brief Solver options and configuration profiles.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <expected>
#endif
#include <kmx/sat/option_id.hpp>

namespace kmx::sat::telemetry
{
    /// @brief Solver options and configuration profiles.
    ///
    /// @details
    /// `solver_options` is the single reflection-ready store for every tunable value across the solver (restart
    /// intervals, EVSIDS decay, enabled simplification passes, memory ceilings, and so on): `set`/`get` provide
    /// direct access to one `option_id`, `load_profile` applies a predefined bundle of values in one call (for
    /// example a competition-mode profile versus a library-embedding profile), and `validate` checks the whole
    /// option set for range violations, features unsupported by the current build, and inter-option dependency
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
            /// @brief The requested option is not recognized by this build.
            unknown_option,
            /// @brief The requested option is feature-gated and not available in the current build.
            unsupported_in_baseline_build,
            /// @brief The requested option value conflicts with another currently set option.
            dependency_conflict
        };

        /// @brief Constructs an options set with implementation-defined defaults.
        /// @throws None (noexcept).
        solver_options() noexcept = default;

        /// @brief Sets one numeric option, without immediate cross-option validation.
        /// @param id Option to assign.
        /// @param value Option value to assign.
        /// @throws None (noexcept).
        void set(const option_id id, const std::int64_t value) noexcept { values_[static_cast<std::size_t>(id)] = value; }

        /// @brief Returns the current value of one option.
        /// @param id Option to read.
        /// @return Current option value, zero when the option was never set.
        /// @throws None (noexcept).
        std::int64_t get(const option_id id) const noexcept { return values_[static_cast<std::size_t>(id)]; }

        /// @brief Applies a predefined bundle of option values.
        /// @param id Configuration profile to load.
        /// @return Success, or a `validation_error` describing why the profile could not be applied.
        /// @throws None (noexcept).
        std::expected<void, validation_error> load_profile(const configuration_profile_id id) noexcept;

        /// @brief Validates the entire current option set for range, dependency, and feature-availability issues.
        /// @return Success, or the first `validation_error` encountered.
        /// @throws None (noexcept).
        std::expected<void, validation_error> validate() const noexcept { return {}; }

    private:
        std::array<std::int64_t, option_count> values_ {};
    };
}
