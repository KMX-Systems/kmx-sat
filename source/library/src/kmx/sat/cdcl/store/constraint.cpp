/// @file library/src/kmx/sat/cdcl/store/constraint.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/store/constraint.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/store/constraint.hpp>

namespace kmx::sat::cdcl::store
{
    optional_literal_span_t constraint::constraint_clause_ref() const noexcept
    {
        if (!has_clause_)
            return {};
        return std::span<const literal> {clause_};
    }
}
