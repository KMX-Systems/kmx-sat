/// @file library/src/kmx/sat/cdcl/otf_strengthener.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/otf_strengthener.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/otf_strengthener.hpp>

namespace kmx::sat::cdcl
{
    bool otf_strengthener::try_strengthen(const clause::ref_t ref) noexcept
    {
        if (!ref.valid())
            return false;

        ++strengthened_clause_count_;
        last_action_ = action::strengthened;
        last_ref_ = ref;
        return true;
    }

    bool otf_strengthener::try_subsume(const clause::ref_t ref) noexcept
    {
        if (!ref.valid())
            return false;

        ++subsumed_clause_count_;
        last_action_ = action::subsumed;
        last_ref_ = ref;
        return true;
    }

    void otf_strengthener::rewrite_reason_if_needed(const clause::ref_t ref) noexcept
    {
        if (!ref.valid())
            return;
        ++rewritten_reason_count_;
        last_rewritten_ref_ = ref;
    }
}
