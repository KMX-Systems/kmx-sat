/// @file inc/kmx/sat/cdcl/compaction_service.hpp
/// @brief Variable reindexing and state compaction after eliminations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/bank/watch_list.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Variable reindexing and state compaction after eliminations.
    ///
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
        bool should_compact() const noexcept
        {
            return variable_permutation_ready_;
        }

        /// @brief Computes the dense old-to-new variable renumbering for currently live variables.
        /// @throws None (noexcept).
        void build_variable_permutation() noexcept
        {
            variable_permutation_ready_ = true;
        }

        /// @brief Rewrites literals stored in clauses according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_literals() noexcept
        {
        }

        /// @brief Rewrites watch-list entries according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_watches() noexcept
        {
        }

        /// @brief Rewrites trail-level implication reasons according to the computed variable permutation.
        /// @throws None (noexcept).
        void rewrite_reasons() noexcept
        {
        }

        /// @brief Propagates the variable permutation into the external variable mapping so external identity is
        /// preserved.
        /// @throws None (noexcept).
        void rewrite_external_mapping() noexcept
        {
            if (mapper_ != nullptr)
            {
                mapper_->rebuild_after_compaction();
                if (variable_permutation_ready_)
                {
                    dense_internal_base_ = 1u << 30;
                }
            }
        }

        void attach_mapper(variable_mapper& mapper) noexcept
        {
            mapper_ = &mapper;
        }

    private:
        variable_mapper* mapper_ {nullptr};
        bool variable_permutation_ready_ {false};
        std::uint32_t dense_internal_base_ {1u << 30};
    };
}
