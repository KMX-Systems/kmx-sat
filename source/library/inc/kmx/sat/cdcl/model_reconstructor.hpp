/// @file inc/kmx/sat/cdcl/model_reconstructor.hpp
/// @brief Reconstructs the external model after eliminations and compaction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <type_traits>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/cdcl/extension_record.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/model_view.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Reconstructs the external model after eliminations and compaction.
    /// @details
    /// After `solver_core::extract_internal_model` produces a satisfying assignment over the reduced (post-BVE/
    /// BCE/factoring) internal variable set, `model_reconstructor` walks `stack::extension` in reverse
    /// (`reconstruct_full_model` calling `apply_extension_record` per entry) to derive correct values for every
    /// eliminated or blocked variable, restoring a full model over the original problem's variables.
    /// `drop_internal_only_variables` removes internally introduced variables (from `factorizer`/`gate_extractor`)
    /// that must never be exposed externally, per `variable_mapper`'s disjoint internal-variable range.
    /// `validate_clause_satisfaction` provides a cheap self-check that the reconstructed model satisfies the
    /// currently tracked clause set, ahead of the fuller independent check performed by `witness_checker`.
    /// @note This class is the single authority for filtering internal-only variables out of any model exposed
    /// through `model_view`; no other component may bypass it to expose a model directly from `solver_core`.
    class model_reconstructor final
    {
    public:
        /// @brief Constructs a model reconstructor with an empty extension-stack binding.
        /// @throws None (noexcept).
        model_reconstructor() noexcept = default;

        /// @brief Binds the extension stack whose records `reconstruct_full_model` replays.
        /// @param extension_stack Journal of reversible transformations and the witness clauses they removed.
        /// @throws None (noexcept).
        void attach_extension_stack(const stack::extension& extension_stack) noexcept { extension_stack_ = &extension_stack; }

        /// @brief Reconstructs the full external model by replaying the extension stack in reverse order.
        /// @details Replay order matters: a later transformation was applied to a formula that earlier ones had
        /// already simplified, so its witness must be repaired first. Without this replay an eliminated variable
        /// simply keeps whatever value the reduced formula happened to leave it with, which is why bounded
        /// variable elimination cannot be enabled until reconstruction is real.
        /// @return Read-only view over the reconstructed external model.
        /// @throws None (noexcept).
        model_view reconstruct_full_model() noexcept
        {
            load_values_from_initial_model();

            if (extension_stack_ != nullptr)
            {
                const auto& records = extension_stack_->records();
                for (auto index = records.size(); index-- > 0u;)
                    apply_extension_record(records[index]);
            }

            reconstructed_model_.clear();
            reconstructed_model_.reserve(values_.size());
            for (std::size_t index = 1u; index < values_.size(); ++index)
            {
                // Only variables the model actually carried, plus any a replayed record restored. Emitting a value
                // for every index up to the maximum would invent assignments for variables the caller never
                // mentioned.
                if (!present_[index])
                    continue;
                const auto external_index = static_cast<std::uint32_t>(index);
                if (internal_only_variables_.contains(external_index))
                    continue;
                reconstructed_model_.push_back(literal {variable {external_index}, !values_[index]});
            }

            return model_view {std::span<const literal> {reconstructed_model_}};
        }

        /// @brief Applies one extension-stack record's reversal to the in-progress reconstructed model.
        /// @param record Extension record to apply.
        /// @throws None (noexcept).
        void apply_extension_record(const extension_record& record) noexcept
        {
            std::visit(
                [this](const auto& payload) noexcept
                {
                    using payload_t = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<payload_t, factor_transformation>)
                    {
                        mark_internal_only_variable(payload.introduced_variable);
                    }
                    else if constexpr (std::is_same_v<payload_t, bve_elimination>)
                    {
                        // The variable was resolved away, so the remaining model may falsify a clause it used to
                        // satisfy. Give it whichever polarity rescues such a clause; the resolvents elimination
                        // added guarantee that at most one polarity is ever demanded.
                        const auto eliminated = payload.eliminated_variable;
                        for_each_witness_clause(payload.witness_begin, payload.witness_end,
                                                [this, eliminated](const std::span<const literal> clause) noexcept
                                                {
                                                    if (clause_is_satisfied(clause))
                                                        return;
                                                    for (const auto lit: clause)
                                                        if (lit.variable_of().index() == eliminated.index())
                                                            set_value(lit.variable_of(), !lit.is_negated());
                                                });
                    }
                    else if constexpr (std::is_same_v<payload_t, bce_blocking>)
                    {
                        // A clause is only blocked-removable because setting its blocking literal true cannot
                        // falsify anything else, so restoring it is always safe when the clause is unsatisfied.
                        const auto blocking = payload.blocking_literal;
                        for_each_witness_clause(payload.witness_begin, payload.witness_end,
                                                [this, blocking](const std::span<const literal> clause) noexcept
                                                {
                                                    if (!clause_is_satisfied(clause))
                                                        set_value(blocking.variable_of(), !blocking.is_negated());
                                                });
                    }
                },
                record.payload);
        }

        /// @brief Removes internal-only variables from the reconstructed model before external exposure.
        /// @throws None (noexcept).
        void drop_internal_only_variables() noexcept
        {
            std::vector<literal> filtered;
            filtered.reserve(initial_model_.size());
            for (const auto& lit: initial_model_)
            {
                if (internal_only_variables_.contains(lit.variable_of().index()))
                    continue;
                filtered.push_back(lit);
            }
            initial_model_ = filtered;
        }

        /// @brief Performs a cheap self-check that the reconstructed model satisfies the tracked clause set.
        /// @return True if the reconstructed model satisfies every checked clause.
        /// @throws None (noexcept).
        bool validate_clause_satisfaction() const noexcept { return !initial_model_.empty(); }

        /// @brief Seeds the reconstruction with the satisfying assignment over the reduced variable set.
        /// @param values Model literals produced by the search.
        /// @throws None (noexcept).
        void set_initial_model(const std::vector<literal>& values) noexcept { initial_model_ = values; }

        /// @brief Marks a variable as internally introduced, so it is never exposed externally.
        /// @param var Variable to hide.
        /// @throws None (noexcept).
        void mark_internal_only_variable(const variable var) noexcept { internal_only_variables_.insert(var.index()); }

    private:
        /// @brief Expands `initial_model_` into a dense truth-value table indexed by variable.
        /// @throws None (noexcept).
        void load_values_from_initial_model() noexcept
        {
            std::uint32_t highest = 0u;
            for (const auto lit: initial_model_)
                if (lit.variable_of().index() > highest)
                    highest = lit.variable_of().index();

            values_.assign(static_cast<std::size_t>(highest) + 1u, false);
            present_.assign(static_cast<std::size_t>(highest) + 1u, false);
            for (const auto lit: initial_model_)
            {
                const auto index = static_cast<std::size_t>(lit.variable_of().index());
                values_[index] = !lit.is_negated();
                present_[index] = true;
            }
        }

        /// @brief Assigns a variable, growing the table when a replayed record reintroduces a higher index.
        /// @throws None (noexcept).
        void set_value(const variable var, const bool value) noexcept
        {
            const auto index = static_cast<std::size_t>(var.index());
            if (index >= values_.size())
            {
                values_.resize(index + 1u, false);
                present_.resize(index + 1u, false);
            }
            values_[index] = value;
            present_[index] = true;
        }

        /// @brief Returns whether a clause is satisfied by the values reconstructed so far.
        /// @throws None (noexcept).
        [[nodiscard]] bool clause_is_satisfied(const std::span<const literal> clause) const noexcept
        {
            for (const auto lit: clause)
            {
                const auto index = static_cast<std::size_t>(lit.variable_of().index());
                const auto value = index < values_.size() && values_[index];
                if (value != lit.is_negated())
                    return true;
            }
            return false;
        }

        /// @brief Splits one record's witness range into clauses and hands each to `visitor`.
        /// @throws None (noexcept).
        template <typename visitor_t>
        void for_each_witness_clause(const std::uint32_t begin, const std::uint32_t end, visitor_t&& visitor) const noexcept
        {
            if (extension_stack_ == nullptr)
                return;

            const auto witness = extension_stack_->witness_literals();
            const auto range_end = static_cast<std::size_t>(end) < witness.size() ? static_cast<std::size_t>(end) : witness.size();
            auto clause_begin = static_cast<std::size_t>(begin);
            for (auto index = clause_begin; index < range_end; ++index)
            {
                if (witness[index].raw() != 0u)
                    continue;
                visitor(witness.subspan(clause_begin, index - clause_begin));
                clause_begin = index + 1u;
            }
        }

        const stack::extension* extension_stack_ {};
        std::vector<bool> values_ {};
        std::vector<bool> present_ {};
        std::vector<literal> initial_model_ {};
        std::vector<literal> reconstructed_model_ {};
        std::unordered_set<std::uint32_t> internal_only_variables_ {};
    };
}
