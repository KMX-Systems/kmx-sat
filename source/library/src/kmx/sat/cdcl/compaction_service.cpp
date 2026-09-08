/// @file library/src/kmx/sat/cdcl/compaction_service.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/compaction_service.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/compaction_service.hpp>

namespace kmx::sat::cdcl
{
    void compaction_service::build_variable_permutation() noexcept
    {
        variable_permutation_.clear();

        if (mapper_ == nullptr)
        {
            variable_permutation_ready_ = false;
            return;
        }

        auto entries = mapper_->mapping_entries();
        std::vector<variable> live_internal_variables {};
        live_internal_variables.reserve(entries.size());
        for (const auto& [external_var, internal_var]: entries)
        {
            (void)external_var;
            if (!mapper_->is_eliminated(internal_var))
                live_internal_variables.push_back(internal_var);
        }

        std::sort(live_internal_variables.begin(), live_internal_variables.end(),
                  [](const variable left, const variable right) noexcept { return left.index() < right.index(); });

        constexpr std::uint32_t dense_internal_base = 1u << 30;
        std::uint32_t next_dense_index = dense_internal_base;
        for (const auto internal_var: live_internal_variables)
            variable_permutation_[internal_var.index()] = variable {next_dense_index++};

        variable_permutation_ready_ = true;
    }

    void compaction_service::rewrite_literals() noexcept
    {
        if (clause_database_ == nullptr)
            return;

        reason_ref_rewrite_.clear();
        relocated_refs_.clear();

        rewrite_database_literals(
            [&](const clause::ref_t ref) noexcept
            {
                auto literals = clause_database_->storage_of().literals_of(ref);
                for (auto& lit: literals)
                    lit = remap_literal(lit);

                const auto relocated_ref = clause_database_->storage_of().relocate_clause(ref);
                if (relocated_ref.valid() && (relocated_ref != ref))
                {
                    clause_database_->rewrite_ref_after_gc(ref, relocated_ref);
                    if (cold_store_ != nullptr)
                        cold_store_->rewrite_ref_after_gc(ref, relocated_ref);
                    if (proof_manager_ != nullptr)
                        proof_manager_->on_clause_relocated(ref, relocated_ref);
                    reason_ref_rewrite_[ref.offset()] = relocated_ref.offset();
                    relocated_refs_.push_back({ref, relocated_ref});
                }
                clause_database_->storage_of().rewrite_clause_literals(relocated_ref, literals);
                if (cold_store_ != nullptr)
                    cold_store_->rewrite_literals_after_compaction(relocated_ref, literals);
            });
    }

    void compaction_service::rewrite_watches() noexcept
    {
        if (watch_list_ != nullptr)
        {
            for (const auto& [old_ref, new_ref]: relocated_refs_)
                watch_list_->replace_clause_ref_after_gc(old_ref, new_ref);
            watch_list_->reindex_after_compaction([&](const literal lit) noexcept { return remap_literal(lit); });
        }
    }

    void compaction_service::rewrite_reasons() noexcept
    {
        if (assignment_store_ == nullptr)
            return;
        assignment_store_->rewrite_reasons_after_compaction(reason_ref_rewrite_);
    }

    void compaction_service::rewrite_external_mapping() noexcept
    {
        if (mapper_ != nullptr)
        {
            mapper_->rebuild_after_compaction(variable_permutation_);
            if (variable_permutation_ready_)
                dense_internal_base_ = 1u << 30;
        }
    }

    literal compaction_service::remap_literal(const literal lit) const noexcept
    {
        if (const auto it = variable_permutation_.find(lit.variable_of().index()); it != variable_permutation_.end())
            return literal {it->second, lit.is_negated()};
        return lit;
    }
}
