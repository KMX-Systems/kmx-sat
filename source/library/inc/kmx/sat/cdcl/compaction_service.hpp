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
    /// @brief Old-to-new clause references recorded by a compaction pass.
    using relocation_list_t = std::vector<std::pair<clause::ref_t, clause::ref_t>>;

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
        void build_variable_permutation() noexcept;

        /// @brief Rewrites literals stored in clauses according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_literals() noexcept;

        /// @brief Rewrites watch-list entries according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_watches() noexcept;

        /// @brief Rewrites trail-level implication reasons according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_reasons() noexcept;

        /// @brief Propagates the variable permutation into the external variable mapping so external identity is
        /// preserved.
        /// @throws None (noexcept).
        void rewrite_external_mapping() noexcept;

        void attach_database(clause::database& clause_database) noexcept { clause_database_ = &clause_database; }

        void attach_watch_list(bank::watch_list& watch_list) noexcept { watch_list_ = &watch_list; }

        void attach_mapper(variable_mapper& mapper) noexcept { mapper_ = &mapper; }

        void attach_assignment_store(store::assignment& assignment_store) noexcept { assignment_store_ = &assignment_store; }

        /// @brief Attaches proof management for stable clause identity during compaction relocation.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Attaches opt-in cold storage whose tracked references must follow compaction relocation.
        void attach_cold_store(store::clause_cold& cold_store) noexcept { cold_store_ = &cold_store; }

    private:
        literal remap_literal(const literal lit) const noexcept;

        template <typename Visitor>
        void rewrite_database_literals(Visitor&& visitor) noexcept
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
        relocation_list_t relocated_refs_ {};
        bool variable_permutation_ready_ {};
        std::uint32_t dense_internal_base_ {1u << 30};
    };
}
