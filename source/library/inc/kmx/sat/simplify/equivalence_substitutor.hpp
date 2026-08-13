/// @file inc/kmx/sat/simplify/equivalence_substitutor.hpp
/// @brief Propagates ELS results across all subsystems.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <unordered_map>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    /// @brief Propagates ELS results across all subsystems.
    ///
    /// Once `engine::decomposition::find_equivalences` determines that a set of literals are all equivalent,
    /// `equivalence_substitutor` is the single place that applies the chosen representative literal everywhere it
    /// matters, so no subsystem is left referencing a superseded literal: `apply_equivalence_class` records the
    /// representative mapping for one equivalence class, `rewrite_clauses` substitutes it into `clause::database`
    /// storage, `rewrite_watches` updates `bank::watch_list` accordingly, and `rewrite_external_mapping` propagates
    /// the substitution into `variable_mapper`/`external_frontend` so externally visible variable identity remains
    /// correct.
    /// @note Like every structural formula transformation, an ELS substitution must also be reported to
    /// `proof::proof_manager` (as resolution-equivalent proof events for DRAT/LRAT/FRAT, or as native equivalence
    /// events when `veripb_tracer` is active) and, where the substituted variable becomes internal-only, to
    /// `model_reconstructor`.
    class equivalence_substitutor final
    {
    public:
        /// @brief Constructs an equivalence substitutor with no pending equivalence classes.
        /// @throws None (noexcept).
        equivalence_substitutor() noexcept = default;

        /// @brief Attaches clause database storage to receive in-place literal substitutions.
        /// @param database Clause database to rewrite.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept { clause_database_ = &database; }

        /// @brief Attaches watch-list storage to receive literal partition and metadata substitutions.
        /// @param watch_list Watch-list bank to rewrite.
        /// @throws None (noexcept).
        void attach_watch_list(cdcl::bank::watch_list& watch_list) noexcept { watch_list_ = &watch_list; }

        /// @brief Attaches variable mapper to propagate substitutions into external/internal mappings.
        /// @param mapper Variable mapper to rewrite.
        /// @throws None (noexcept).
        void attach_variable_mapper(cdcl::variable_mapper& mapper) noexcept { variable_mapper_ = &mapper; }

        /// @brief Records the representative-literal mapping for one discovered equivalence class.
        /// @throws None (noexcept).
        void apply_equivalence_class() noexcept { apply_equivalence_class(equivalence_class_count_ + 1u, equivalence_class_count_ + 2u); }

        /// @brief Records a representative mapping from one literal/raw id to another.
        /// @param from Raw identifier being replaced.
        /// @param to Raw identifier chosen as representative.
        /// @throws None (noexcept).
        void apply_equivalence_class(const std::uint32_t from, const std::uint32_t to) noexcept
        {
            if (from == 0u || to == 0u || from == to)
            {
                return;
            }

            pending_rewrites_.push_back({from, to});
            rewrite_map_[from] = to;
            ++equivalence_class_count_;
            rewrite_completed_ = false;
        }

        /// @brief Rewrites clause storage to use each equivalence class's representative literal.
        /// @throws None (noexcept).
        void rewrite_clauses() noexcept
        {
            if (!pending_rewrites_.empty())
            {
                if (clause_database_ != nullptr)
                {
                    rewrite_clause_set(clause_database_->irredundant_refs());
                    rewrite_clause_set(clause_database_->redundant_refs());
                }
                ++clause_rewrite_count_;
            }
        }

        /// @brief Rewrites watch-list entries to use each equivalence class's representative literal.
        /// @throws None (noexcept).
        void rewrite_watches() noexcept
        {
            if (!pending_rewrites_.empty())
            {
                if (watch_list_ != nullptr)
                {
                    watch_list_->reindex_after_compaction([&](const literal old_literal) noexcept { return remap_literal(old_literal); });
                }
                ++watch_rewrite_count_;
            }
        }

        /// @brief Propagates the substitution into the external variable mapping.
        /// @throws None (noexcept).
        void rewrite_external_mapping() noexcept
        {
            if (!pending_rewrites_.empty())
            {
                if (variable_mapper_ != nullptr)
                {
                    std::unordered_map<std::uint32_t, variable> permutation {};
                    permutation.reserve(pending_rewrites_.size());
                    for (const auto& [from, to]: pending_rewrites_)
                    {
                        permutation[from] = variable {resolve_representative(to)};
                    }
                    variable_mapper_->rebuild_after_compaction(permutation);
                }
                ++external_mapping_rewrite_count_;
                last_applied_rewrite_count_ = pending_rewrites_.size();
                pending_rewrites_.clear();
                rewrite_completed_ = true;
            }
        }

        std::size_t equivalence_class_count() const noexcept { return equivalence_class_count_; }

        std::size_t clause_rewrite_count() const noexcept { return clause_rewrite_count_; }

        std::size_t watch_rewrite_count() const noexcept { return watch_rewrite_count_; }

        std::size_t external_mapping_rewrite_count() const noexcept { return external_mapping_rewrite_count_; }

        bool rewrite_completed() const noexcept { return rewrite_completed_; }

        std::size_t pending_rewrite_count() const noexcept { return pending_rewrites_.size(); }

        std::size_t last_applied_rewrite_count() const noexcept { return last_applied_rewrite_count_; }

    private:
        std::uint32_t resolve_representative(std::uint32_t value) const noexcept
        {
            std::size_t guard {};
            while (guard++ < rewrite_map_.size())
            {
                const auto it = rewrite_map_.find(value);
                if (it == rewrite_map_.end() || it->second == value)
                {
                    break;
                }
                value = it->second;
            }
            return value;
        }

        literal remap_literal(const literal old_literal) const noexcept
        {
            const auto old_var = old_literal.variable_of().index();
            const auto new_var = resolve_representative(old_var);
            if (new_var == old_var)
            {
                return old_literal;
            }
            return literal {variable {new_var}, old_literal.is_negated()};
        }

        void rewrite_clause_set(const std::span<const cdcl::clause::ref_t> refs) noexcept
        {
            auto& storage = clause_database_->storage_of();
            for (const auto ref: refs)
            {
                if (!ref.valid() || clause_database_->is_garbage(ref))
                {
                    continue;
                }

                auto literals = storage.literals_of(ref);
                bool changed {};
                for (auto& lit: literals)
                {
                    const auto remapped = remap_literal(lit);
                    if (remapped != lit)
                    {
                        lit = remapped;
                        changed = true;
                    }
                }

                if (changed)
                {
                    storage.rewrite_clause_literals(ref, literals);
                }
            }
        }

        cdcl::clause::database* clause_database_ {};
        cdcl::bank::watch_list* watch_list_ {};
        cdcl::variable_mapper* variable_mapper_ {};
        std::vector<std::pair<std::uint32_t, std::uint32_t>> pending_rewrites_ {};
        std::unordered_map<std::uint32_t, std::uint32_t> rewrite_map_ {};
        std::size_t equivalence_class_count_ {};
        std::size_t clause_rewrite_count_ {};
        std::size_t watch_rewrite_count_ {};
        std::size_t external_mapping_rewrite_count_ {};
        std::size_t last_applied_rewrite_count_ {};
        bool rewrite_completed_ {};
    };
}
