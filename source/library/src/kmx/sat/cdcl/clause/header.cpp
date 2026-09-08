/// @file library/src/kmx/sat/cdcl/clause/header.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/clause/header.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/clause/header.hpp>

namespace kmx::sat::cdcl::clause
{
    std::uint8_t header::compose_flags(const bool redundant, const bool garbage, const bool reason, const bool shrunken) noexcept
    {
        std::uint8_t flags {};
        if (redundant)
            flags |= redundant_flag;
        if (garbage)
            flags |= garbage_flag;
        if (reason)
            flags |= reason_flag;
        if (shrunken)
            flags |= shrunken_flag;
        return flags;
    }

    void header::set_flag(const std::uint8_t flag, const bool enabled) noexcept
    {
        if (enabled)
        {
            flags_ |= flag;
            return;
        }
        flags_ &= static_cast<std::uint8_t>(~flag);
    }
}
