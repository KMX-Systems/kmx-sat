/// @file library/src/kmx/sat/cdcl/garbage_collector.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/garbage_collector.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/garbage_collector.hpp>

namespace kmx::sat::cdcl
{
    void garbage_collector::collect() noexcept
    {
        relocated_clause_count_ = 0u;
        watch_rewrite_count_ = 0u;
        reason_rewrite_count_ = 0u;
        proof_relocation_count_ = 0u;
        finalized_ = false;
        pending_live_refs_.clear();
        relocated_refs_.clear();

        if (clause_database_ == nullptr)
            return;

        clause_database_->iterate_irredundant([&](const clause::ref_t ref) noexcept { pending_live_refs_.push_back(ref); });
        clause_database_->iterate_redundant([&](const clause::ref_t ref) noexcept { pending_live_refs_.push_back(ref); });
    }

    void garbage_collector::relocate_live_clause() noexcept
    {
        if ((clause_database_ != nullptr) && !pending_live_refs_.empty())
        {
            const auto old_ref = pending_live_refs_.front();
            pending_live_refs_.erase(pending_live_refs_.begin());
            const auto new_ref = clause_database_->storage_of().relocate_clause(old_ref);
            if (new_ref.valid() && (new_ref != old_ref))
            {
                clause_database_->rewrite_ref_after_gc(old_ref, new_ref);
                if (cold_store_ != nullptr)
                    cold_store_->rewrite_ref_after_gc(old_ref, new_ref);
                if (proof_manager_ != nullptr)
                {
                    proof_manager_->on_clause_relocated(old_ref, new_ref);
                    ++proof_relocation_count_;
                }
                relocated_refs_.push_back({old_ref, new_ref});
                ++relocated_clause_count_;
            }
        }
    }

    void garbage_collector::rewrite_watchers() noexcept
    {
        if (watch_list_ != nullptr)
        {
            for (const auto& [old_ref, new_ref]: relocated_refs_)
                watch_list_->replace_clause_ref_after_gc(old_ref, new_ref);
            watch_rewrite_count_ += relocated_refs_.size();
        }
    }

    void garbage_collector::rewrite_reasons() noexcept
    {
        if ((assignment_store_ == nullptr) || relocated_refs_.empty())
            return;

        std::unordered_map<clause::ref_t::offset_t, clause::ref_t::offset_t> relocation {};
        relocation.reserve(relocated_refs_.size());
        for (const auto& [old_ref, new_ref]: relocated_refs_)
            relocation[old_ref.offset()] = new_ref.offset();

        reason_rewrite_count_ = 0u;
        assignment_store_->iterate_reasons(
            [&](const clause::ref_t reason_ref) noexcept
            {
                if (relocation.find(reason_ref.offset()) != relocation.end())
                    ++reason_rewrite_count_;
            });
        assignment_store_->rewrite_reasons_after_compaction(relocation);
    }
}
