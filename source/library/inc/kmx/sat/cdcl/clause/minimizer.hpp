/// @file inc/kmx/sat/cdcl/clause/minimizer.hpp
/// @brief Learned-clause minimization and clause-quality recomputation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <unordered_map>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/cdcl/clause/storage.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief Learned-clause minimization and clause-quality recomputation.
    ///
    /// A freshly 1-UIP-derived clause from `conflict_analyzer` often contains literals that are themselves
    /// implied by other literals already in the clause; `minimize_learned_clause` removes such redundant literals
    /// through recursive/self-subsuming resolution against the implication graph (the MiniSat/Kissat-style
    /// minimization pass), and `shrink_clause` commits the result in place via `clause::storage::shrink_clause`.
    /// `recompute_glue` updates the clause's literal-blocks-distance metric after minimization changes its literal
    /// set, since glue is computed from the set of distinct decision levels touched by the clause; `promote_if_needed`
    /// moves the clause to a higher `clause::database` tier when the recomputed glue crosses a quality threshold.
    class minimizer final
    {
    public:
        /// @brief Constructs a minimizer with no clause-specific state.
        /// @throws None (noexcept).
        minimizer() noexcept = default;

        void attach_storage(storage& storage) noexcept
        {
            storage_ = &storage;
        }

        /// @brief Removes literals from a learned clause that are implied by its other literals.
        /// @param ref Reference to the learned clause to minimize.
        /// @throws None (noexcept).
        void minimize_learned_clause(const ref_t ref) noexcept
        {
            if (storage_ == nullptr || !ref.valid())
            {
                return;
            }

            auto literals = storage_->literals_of(ref);
            if (literals.size() <= 1u)
            {
                return;
            }

            std::vector<literal> deduplicated {};
            deduplicated.reserve(literals.size());
            std::unordered_set<literal::raw_t> seen {};
            for (const auto lit : literals)
            {
                if (seen.insert(lit.raw()).second)
                {
                    deduplicated.push_back(lit);
                }
            }

            if (!deduplicated.empty())
            {
                target_sizes_[ref.offset()] = static_cast<std::uint32_t>(deduplicated.size());
                minimized_.insert(ref.offset());
            }
        }

        /// @brief Commits a clause's reduced literal set in place after minimization.
        /// @param ref Reference to the clause to shrink.
        /// @throws None (noexcept).
        void shrink_clause(const ref_t ref) noexcept
        {
            if (storage_ == nullptr || !ref.valid())
            {
                return;
            }

            if (minimized_.find(ref.offset()) != minimized_.end())
            {
                const auto target_size = target_sizes_.contains(ref.offset()) ? target_sizes_.at(ref.offset()) : 1u;
                storage_->shrink_clause(ref, target_size);
                shrunk_.insert(ref.offset());
            }
        }

        /// @brief Recomputes the glue (literal blocks distance) metric for a clause after its literals changed.
        /// @param ref Reference to the clause to recompute.
        /// @throws None (noexcept).
        void recompute_glue(const ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return;
            }
            const auto target_size = target_sizes_.contains(ref.offset()) ? target_sizes_.at(ref.offset()) : 1u;
            last_glue_ = std::max<std::uint32_t>(1u, target_size);
            glue_.insert_or_assign(ref.offset(), last_glue_);
        }

        /// @brief Promotes a clause to a higher-quality tier if its recomputed glue justifies it.
        /// @param ref Reference to the clause to evaluate.
        /// @throws None (noexcept).
        void promote_if_needed(const ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return;
            }
            if (glue_.contains(ref.offset()) && glue_.at(ref.offset()) <= 2u)
            {
                promoted_.insert(ref.offset());
            }
        }

        [[nodiscard]] std::uint32_t minimized_clause_count() const noexcept
        {
            return static_cast<std::uint32_t>(minimized_.size());
        }

        [[nodiscard]] std::uint32_t shrunk_clause_count() const noexcept
        {
            return static_cast<std::uint32_t>(shrunk_.size());
        }

        [[nodiscard]] std::uint32_t promoted_clause_count() const noexcept
        {
            return static_cast<std::uint32_t>(promoted_.size());
        }

        [[nodiscard]] std::uint32_t last_glue() const noexcept
        {
            return last_glue_;
        }

    private:
        storage* storage_ {nullptr};
        std::unordered_map<ref_t::offset_t, std::uint32_t> glue_ {};
        std::unordered_map<ref_t::offset_t, std::uint32_t> target_sizes_ {};
        std::unordered_set<ref_t::offset_t> minimized_ {};
        std::unordered_set<ref_t::offset_t> shrunk_ {};
        std::unordered_set<ref_t::offset_t> promoted_ {};
        std::uint32_t last_glue_ {0u};
    };
}
