/// @file library/src/kmx/sat/simplify/flush_restore_manager.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/flush_restore_manager.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/flush_restore_manager.hpp>

namespace kmx::sat::simplify
{
    void flush_restore_manager::flush_redundant() noexcept
    {
        ++flush_count_;
        if (database_ == nullptr)
            return;

        flush_matching([&](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
    }

    void flush_restore_manager::restore_all() noexcept
    {
        ++restore_count_;
        if (database_ == nullptr)
            return;

        restore_matching([](const flushed_clause&) noexcept { return true; });
    }

    void flush_restore_manager::restore_irredundant_only() noexcept
    {
        ++restore_count_;
        if (database_ == nullptr)
            return;

        restore_matching([](const flushed_clause& record) noexcept { return !record.redundant; });
    }
}
