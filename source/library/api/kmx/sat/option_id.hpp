/// @file api/kmx/sat/option_id.hpp
/// @brief Closed sets naming the tunable solver options and the predefined configuration profiles.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <string_view>
#endif

namespace kmx::sat
{
    /// @brief One tunable solver option.
    /// @details Every option the facade recognizes is an enumerator here, so `solver::set_option`,
    /// `telemetry::solver_options` and the command line all select the same option by identity. The spelled-out
    /// names in `option_names` exist only for the external boundaries that must accept or emit text (the C API and
    /// serialized replay traces); nothing inside the library dispatches on them.
    enum class option_id : std::uint8_t
    {
        conflict_limit,
        decision_limit,
        enabled_pass_mask,
        strict_mode,
        statistics_verbose_reporting,
        decision_conflict_maintenance_interval,
        decision_chb_decay_interval,
        decision_restart_decay_interval,
        restart_interval,
        decision_restart_interval,
        reduction_interval,
        local_search_flips_per_variable,
        local_search_effort_percent,
        inprocess_conflict_window,
        chb_enabled,
        reduction_fraction_percent,
        glue_restart_threshold_percent,
        cold_storage_enabled,
        activity_retention_threshold_percent,
    };

    /// @brief Number of options in `option_id`, and the size of any per-option table.
    inline constexpr std::size_t option_count {static_cast<std::size_t>(option_id::activity_retention_threshold_percent) + 1u};

    /// @brief A predefined bundle of option values applied in one call.
    enum class configuration_profile_id : std::uint8_t
    {
        /// @brief Strict validation, every simplification pass disabled.
        safe,
        /// @brief Relaxed validation with every simplification pass available.
        balanced,
        /// @brief Conflict- and decision-limited episodes.
        bounded,
        /// @brief Relaxed validation with every pass available and no episode limits.
        aggressive,
    };

    /// @brief Number of profiles in `configuration_profile_id`.
    inline constexpr std::size_t configuration_profile_count {static_cast<std::size_t>(configuration_profile_id::aggressive) + 1u};

    /// @brief External spelling of each option, indexed by `option_id`.
    inline constexpr std::array<std::string_view, option_count> option_names {"conflict_limit",
                                                                              "decision_limit",
                                                                              "enabled_pass_mask",
                                                                              "strict_mode",
                                                                              "statistics_verbose_reporting",
                                                                              "decision_conflict_maintenance_interval",
                                                                              "decision_chb_decay_interval",
                                                                              "decision_restart_decay_interval",
                                                                              "restart_interval",
                                                                              "decision_restart_interval",
                                                                              "reduction_interval",
                                                                              "local_search_flips_per_variable",
                                                                              "local_search_effort_percent",
                                                                              "inprocess_conflict_window",
                                                                              "chb_enabled",
                                                                              "reduction_fraction_percent",
                                                                              "glue_restart_threshold_percent",
                                                                              "cold_storage_enabled",
                                                                              "activity_retention_threshold_percent"};

    /// @brief External spelling of each configuration profile, indexed by `configuration_profile_id`.
    inline constexpr std::array<std::string_view, configuration_profile_count> configuration_profile_names {"safe", "balanced", "bounded",
                                                                                                            "aggressive"};

    /// @brief Returns the external spelling of one option.
    /// @param id Option to name.
    /// @return Option name as accepted at the C API boundary.
    /// @throws None (noexcept).
    constexpr std::string_view name_of(const option_id id) noexcept
    {
        return option_names[static_cast<std::uint8_t>(id)];
    }

    /// @brief Returns the external spelling of one configuration profile.
    /// @param id Profile to name.
    /// @return Profile name as accepted at the C API boundary.
    /// @throws None (noexcept).
    constexpr std::string_view name_of(const configuration_profile_id id) noexcept
    {
        return configuration_profile_names[static_cast<std::uint8_t>(id)];
    }

    /// @brief Maps an option name arriving from outside the library onto its identifier.
    /// @param name Option name to resolve.
    /// @return Resolved option, or an empty optional when the name is not recognized.
    /// @throws None (noexcept).
    constexpr std::optional<option_id> parse_option_id(const std::string_view name) noexcept
    {
        for (std::size_t index {}; index < option_names.size(); ++index)
            if (option_names[index] == name)
                return static_cast<option_id>(index);
        return {};
    }

    /// @brief Maps a profile name arriving from outside the library onto its identifier.
    /// @param name Profile name to resolve.
    /// @return Resolved profile, or an empty optional when the name is not recognized.
    /// @throws None (noexcept).
    constexpr std::optional<configuration_profile_id> parse_configuration_profile_id(const std::string_view name) noexcept
    {
        for (std::size_t index {}; index < configuration_profile_names.size(); ++index)
            if (configuration_profile_names[index] == name)
                return static_cast<configuration_profile_id>(index);
        return {};
    }
}
