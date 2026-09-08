/// @file library/src/kmx/sat/proof/checker/online.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/checker/online.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/checker/online.hpp>

namespace kmx::sat::proof::checker
{
    void online::on_add(const cdcl::clause::ref_t ref) noexcept
    {
        if (!ref.valid() || !active_.insert(ref.offset()).second)
        {
            ++coverage_.structural_errors;
            return;
        }
        ++coverage_.clauses_added;
    }

    void online::on_delete(const cdcl::clause::ref_t ref) noexcept
    {
        if (!ref.valid() || (active_.erase(ref.offset()) == 0u))
        {
            ++coverage_.structural_errors;
            return;
        }
        ++coverage_.clauses_deleted;
    }

    void online::on_shrink(const cdcl::clause::ref_t ref) noexcept
    {
        if (!ref.valid() || (active_.find(ref.offset()) == active_.end()))
        {
            ++coverage_.structural_errors;
            return;
        }
        ++coverage_.clauses_shrunk;
    }

    void online::on_relocate(const cdcl::clause::ref_t old_ref, const cdcl::clause::ref_t new_ref) noexcept
    {
        if (!old_ref.valid() || !new_ref.valid() || (old_ref.offset() == new_ref.offset()))
            return;
        if (active_.erase(old_ref.offset()) != 0u)
            active_.insert(new_ref.offset());
    }
}
