/// @file inc/kmx/sat/cdcl/compaction_service.hpp
/// @brief Variable reindexing and state compaction after eliminations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Variable reindexing and state compaction after eliminations.
    /// @details
    /// After `bounded_variable_eliminator`/`blocked_clause_eliminator` remove enough variables, sparsely-numbered
    /// remaining variables waste cache lines and array capacity; `compaction_service` renumbers the live variable set
    /// densely. `should_compact` decides whether the eliminated fraction justifies a pass; `build_variable_permutation`
    /// computes the old-to-new variable mapping; `rewrite_literals`, `rewrite_watches`, and `rewrite_reasons` apply
    /// that permutation to clause storage, `bank::watch_list`, and trail-level reason references respectively; and
    /// `rewrite_external_mapping` propagates the same permutation into `variable_mapper`/`external_frontend` so
    /// externally visible variable identity survives the internal renumbering.
    /// @warning Every one of `rewrite_literals`, `rewrite_watches`, `rewrite_reasons`, and `rewrite_external_mapping`
    /// must complete before the search resumes; leaving any one of them stale after `build_variable_permutation`
    /// corrupts propagation, conflict analysis, or the external mapping respectively.
    class compaction_service final
    {
    public:
        /// @brief Constructs a compaction service with no pending permutation.
        /// @throws None (noexcept).
        compaction_service() noexcept = default;

        /// @brief Checks whether the current eliminated-variable fraction justifies a compaction pass.
        /// @return True if compaction should run now.
        /// @throws None (noexcept).
        bool should_compact() const noexcept { return variable_permutation_ready_ && !variable_permutation_.empty(); }

        /// @brief Computes the dense old-to-new variable renumbering for currently live variables.
        /// @throws None (noexcept).
        void build_variable_permutation() noexcept
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
                (void) external_var;
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

        /// @brief Rewrites literals stored in clauses according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_literals() noexcept
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
                    if (relocated_ref.valid() && relocated_ref != ref)
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

        /// @brief Rewrites watch-list entries according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_watches() noexcept
        {
            if (watch_list_ != nullptr)
            {
                for (const auto& [old_ref, new_ref]: relocated_refs_)
                    watch_list_->replace_clause_ref_after_gc(old_ref, new_ref);
                watch_list_->reindex_after_compaction([&](const literal lit) noexcept { return remap_literal(lit); });
            }
        }

        /// @brief Rewrites trail-level implication reasons according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_reasons() noexcept
        {
            if (assignment_store_ == nullptr)
                return;
            assignment_store_->rewrite_reasons_after_compaction(reason_ref_rewrite_);
        }

        /// @brief Propagates the variable permutation into the external variable mapping so external identity is
        /// preserved.
        /// @throws None (noexcept).
        void rewrite_external_mapping() noexcept
        {
            if (mapper_ != nullptr)
            {
                mapper_->rebuild_after_compaction(variable_permutation_);
                if (variable_permutation_ready_)
                    dense_internal_base_ = 1u << 30;
            }
        }

        void attach_database(clause::database& clause_database) noexcept { clause_database_ = &clause_database; }

        void attach_watch_list(bank::watch_list& watch_list) noexcept { watch_list_ = &watch_list; }

        void attach_mapper(variable_mapper& mapper) noexcept { mapper_ = &mapper; }

        void attach_assignment_store(store::assignment& assignment_store) noexcept { assignment_store_ = &assignment_store; }

        /// @brief Attaches proof management for stable clause identity during compaction relocation.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Attaches opt-in cold storage whose tracked references must follow compaction relocation.
        void attach_cold_store(store::clause_cold& cold_store) noexcept { cold_store_ = &cold_store; }

    private:
        literal remap_literal(const literal lit) const noexcept
        {
            if (const auto it = variable_permutation_.find(lit.variable_of().index()); it != variable_permutation_.end())
                return literal {it->second, lit.is_negated()};
            return lit;
        }

        template <typename visitor_t>
        void rewrite_database_literals(visitor_t&& visitor) noexcept
        {
            std::vector<clause::ref_t> references {};
            const auto irredundant = clause_database_->irredundant_refs();
            const auto redundant = clause_database_->redundant_refs();
            references.reserve(irredundant.size() + redundant.size());
            references.insert(references.end(), irredundant.begin(), irredundant.end());
            references.insert(references.end(), redundant.begin(), redundant.end());
            for (const auto ref: references)
                if (!clause_database_->is_garbage(ref))
                    visitor(ref);
        }

        clause::database* clause_database_ {};
        bank::watch_list* watch_list_ {};
        store::assignment* assignment_store_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        store::clause_cold* cold_store_ {};
        variable_mapper* mapper_ {};
        std::unordered_map<std::uint32_t, variable> variable_permutation_ {};
        std::unordered_map<clause::ref_t::offset_t, clause::ref_t::offset_t> reason_ref_rewrite_ {};
        std::vector<std::pair<clause::ref_t, clause::ref_t>> relocated_refs_ {};
        bool variable_permutation_ready_ {};
        std::uint32_t dense_internal_base_ {1u << 30};
    };
}
