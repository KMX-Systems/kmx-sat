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
#include <kmx/sat/cdcl/clause/database.hpp>
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
        /// @brief Non-owning callback returning the current decision level of a variable.
        using level_lookup_t = std::uint32_t (*)(const void* context, variable var) noexcept;

        /// @brief Non-owning callback returning the reason clause of a variable.
        using reason_lookup_t = std::span<const literal> (*)(const void* context, variable var) noexcept;

        /// @brief Constructs a minimizer with no clause-specific state.
        /// @throws None (noexcept).
        minimizer() noexcept = default;

        void attach_storage(storage& storage) noexcept { storage_ = &storage; }

        void attach_database(database& database) noexcept { database_ = &database; }

        /// @brief Removes literals from a learned clause that are implied by its other literals.
        /// @param ref Reference to the learned clause to minimize.
        /// @throws None (noexcept).
        void minimize_learned_clause(const ref_t ref) noexcept
        {
            if (storage_ == nullptr || !ref.valid())
                return;

            auto literals = storage_->literals_of(ref);
            if (literals.size() <= 1u)
                return;

            std::vector<literal> deduplicated {};
            deduplicated.reserve(literals.size());
            std::unordered_set<literal::raw_t> seen {};
            for (const auto lit: literals)
                if (seen.insert(lit.raw()).second)
                    deduplicated.push_back(lit);

            if (!deduplicated.empty())
            {
                target_sizes_[ref.offset()] = static_cast<std::uint32_t>(deduplicated.size());
                minimized_.insert(ref.offset());
            }
        }

        /// @brief Minimizes a learned clause through recursive reason-closure checks.
        /// @param ref Clause to minimize.
        /// @param level_of Callback returning a variable's current decision level.
        /// @param reason_of Callback returning a variable's implication reason.
        /// @param context Opaque callback context passed to both callbacks.
        /// @throws None (noexcept).
        void minimize_learned_clause(const ref_t ref, const level_lookup_t level_of, const reason_lookup_t reason_of,
                                     const void* context) noexcept
        {
            if (storage_ == nullptr || !ref.valid() || level_of == nullptr || reason_of == nullptr)
            {
                minimize_learned_clause(ref);
                return;
            }

            const auto literals = storage_->literals_of(ref);
            if (literals.size() <= 1u)
                return;

            std::unordered_set<literal::raw_t> exact_literals {};
            exact_literals.reserve(literals.size());
            for (const auto lit: literals)
                exact_literals.insert(lit.raw());

            std::unordered_set<std::uint32_t> removable_variables {};
            std::unordered_set<std::uint32_t> visiting_variables {};
            const auto can_remove = [&](const auto& self, const variable var) noexcept -> bool
            {
                const auto variable_index = var.index();
                if (removable_variables.contains(variable_index))
                    return true;
                if (!visiting_variables.insert(variable_index).second)
                    return false;

                const auto reason_view = reason_of(context, var);
                if (reason_view.empty())
                {
                    visiting_variables.erase(variable_index);
                    return false;
                }

                const std::vector<literal> reason {reason_view.begin(), reason_view.end()};
                for (const auto reason_literal: reason)
                {
                    if (reason_literal.variable_of().index() == variable_index || level_of(context, reason_literal.variable_of()) == 0u ||
                        exact_literals.contains(reason_literal.raw()))
                        continue;

                    if (!self(self, reason_literal.variable_of()))
                    {
                        visiting_variables.erase(variable_index);
                        return false;
                    }
                }

                visiting_variables.erase(variable_index);
                removable_variables.insert(variable_index);
                return true;
            };

            std::vector<literal> minimized_literals {};
            minimized_literals.reserve(literals.size());
            minimized_literals.push_back(literals.front());
            for (std::size_t index = 1u; index < literals.size(); ++index)
            {
                const auto lit = literals[index];
                if (!can_remove(can_remove, lit.variable_of()))
                    minimized_literals.push_back(lit);
            }

            if (minimized_literals.size() < literals.size())
            {
                storage_->rewrite_clause_literals(ref, minimized_literals);
                target_sizes_[ref.offset()] = static_cast<std::uint32_t>(minimized_literals.size());
                minimized_.insert(ref.offset());
            }
            else
            {
                minimize_learned_clause(ref);
            }
        }

        /// @brief Commits a clause's reduced literal set in place after minimization.
        /// @param ref Reference to the clause to shrink.
        /// @throws None (noexcept).
        void shrink_clause(const ref_t ref) noexcept
        {
            if (storage_ == nullptr || !ref.valid())
                return;

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
                return;
            const auto target_size = target_sizes_.contains(ref.offset()) ? target_sizes_.at(ref.offset()) : 1u;
            last_glue_ = std::max<std::uint32_t>(1u, target_size);
            glue_.insert_or_assign(ref.offset(), last_glue_);
            if (database_ != nullptr)
                database_->set_glue(ref, last_glue_);
        }

        /// @brief Recomputes exact LBD/glue as the number of distinct decision levels in the stored clause.
        /// @param ref Clause whose current literals should be measured.
        /// @param level_of Callback returning a literal variable's decision level.
        /// @param context Opaque callback context.
        /// @throws None (noexcept).
        void recompute_glue(const ref_t ref, const level_lookup_t level_of, const void* context) noexcept
        {
            if (storage_ == nullptr || !ref.valid() || level_of == nullptr)
                return;

            const auto literals = storage_->literals_of(ref);
            std::unordered_set<std::uint32_t> levels {};
            levels.reserve(literals.size());
            for (const auto lit: literals)
                levels.insert(level_of(context, lit.variable_of()));

            last_glue_ = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(levels.size()));
            glue_.insert_or_assign(ref.offset(), last_glue_);
            if (database_ != nullptr)
                database_->set_glue(ref, last_glue_);
        }

        /// @brief Promotes a clause to a higher-quality tier if its recomputed glue justifies it.
        /// @param ref Reference to the clause to evaluate.
        /// @throws None (noexcept).
        void promote_if_needed(const ref_t ref) noexcept
        {
            if (!ref.valid())
                return;
            if (glue_.contains(ref.offset()) && glue_.at(ref.offset()) <= 2u)
            {
                promoted_.insert(ref.offset());
                if (database_ != nullptr)
                    database_->promote_clause(ref);
            }
        }

        [[nodiscard]] std::uint32_t minimized_clause_count() const noexcept { return static_cast<std::uint32_t>(minimized_.size()); }

        [[nodiscard]] std::uint32_t shrunk_clause_count() const noexcept { return static_cast<std::uint32_t>(shrunk_.size()); }

        [[nodiscard]] std::uint32_t promoted_clause_count() const noexcept { return static_cast<std::uint32_t>(promoted_.size()); }

        /// @brief Returns whether a specific clause was physically shrunk by this minimizer.
        bool was_shrunk(const ref_t ref) const noexcept { return ref.valid() && shrunk_.find(ref.offset()) != shrunk_.end(); }

        [[nodiscard]] std::uint32_t last_glue() const noexcept { return last_glue_; }

    private:
        storage* storage_ {};
        database* database_ {};
        std::unordered_map<ref_t::offset_t, std::uint32_t> glue_ {};
        std::unordered_map<ref_t::offset_t, std::uint32_t> target_sizes_ {};
        std::unordered_set<ref_t::offset_t> minimized_ {};
        std::unordered_set<ref_t::offset_t> shrunk_ {};
        std::unordered_set<ref_t::offset_t> promoted_ {};
        std::uint32_t last_glue_ {};
    };
}
