/// @file library/src/kmx/sat/cdcl/clause/view.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/clause/view.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/clause/view.hpp>

namespace kmx::sat::cdcl::clause
{
    bool view::contains(const literal lit) const noexcept
    {
        for (const auto& entry: literals_)
            if (entry == lit)
                return true;
        return false;
    }
}
