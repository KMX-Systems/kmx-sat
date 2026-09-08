/// @file library/src/kmx/sat/telemetry/solver_options.cpp
/// @brief Out-of-line definitions declared by kmx/sat/telemetry/solver_options.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/telemetry/solver_options.hpp>

namespace kmx::sat::telemetry
{
    std::expected<void, solver_options::validation_error> solver_options::load_profile(const configuration_profile_id id) noexcept
    {
        // The values mirror `solver::set_configuration`, so a caller that stages options here and one that applies
        // the profile straight to the facade end up with the same configuration.
        switch (id)
        {
            case configuration_profile_id::safe:
                set(option_id::strict_mode, 1L);
                set(option_id::enabled_pass_mask, 0L);
                break;
            case configuration_profile_id::balanced:
                set(option_id::strict_mode, 0L);
                break;
            case configuration_profile_id::bounded:
                set(option_id::conflict_limit, 1000L);
                set(option_id::decision_limit, 10000L);
                break;
            case configuration_profile_id::aggressive:
                set(option_id::strict_mode, 0L);
                set(option_id::conflict_limit, 0L);
                set(option_id::decision_limit, 0L);
                break;
        }
        return {};
    }
}
