/// @file library/src/kmx/sat/simplify/vivifier.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/vivifier.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/vivifier.hpp>

namespace kmx::sat::simplify
{
    void vivifier::run() noexcept
    {
        run_completed_ = false;
        processed_in_current_run_ = 0u;

        if (database_ != nullptr)
        {
            bool stopped {};
            auto process_ref = [&](const cdcl::clause::ref_t ref) noexcept
            {
                if (stopped || abort_on_budget())
                {
                    stopped = true;
                    return;
                }

                vivify_clause(ref);
                commit_shrunk_clause(ref);
                ++processed_in_current_run_;
            };

            database_->iterate_irredundant(process_ref);
            database_->iterate_redundant(process_ref);
        }

        run_completed_ = true;
    }

    void vivifier::vivify_clause(const cdcl::clause::ref_t ref) noexcept
    {
        ++vivified_clause_count_;

        pending_shrink_ref_ = {};
        pending_target_size_ = 0u;

        auto& storage = database_->storage_of();
        if ((database_ == nullptr) || !ref.valid() || !storage.is_alive(ref))
            return;

        const auto current_size = storage.literal_count(ref);
        if (current_size <= 1u)
            return;
    }

    void vivifier::commit_shrunk_clause(const cdcl::clause::ref_t ref) noexcept
    {
        if ((database_ == nullptr) || !ref.valid() || (ref != pending_shrink_ref_) || (pending_target_size_ == 0u))
            return;

        database_->storage_of().shrink_clause(ref, pending_target_size_);
        pending_shrink_ref_ = {};
        pending_target_size_ = 0u;
        ++committed_shrink_count_;
    }
}
