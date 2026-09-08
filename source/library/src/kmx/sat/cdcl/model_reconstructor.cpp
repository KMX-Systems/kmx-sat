/// @file library/src/kmx/sat/cdcl/model_reconstructor.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/model_reconstructor.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/model_reconstructor.hpp>

namespace kmx::sat::cdcl
{
    model_view model_reconstructor::reconstruct_full_model() noexcept
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

    void model_reconstructor::apply_extension_record(const extension_record& record) noexcept
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
                    // satisfy. The witness holds only the clauses it occurred in positively: false satisfies
                    // the negative ones outright, and if any positive one is otherwise unsatisfied, true
                    // rescues it without endangering the rest.
                    const auto eliminated = payload.eliminated_variable;
                    // Force it false, overriding whatever the reduced model carried. That is not a default
                    // for a missing value, it is the first half of the reconstruction rule: false satisfies
                    // every clause the variable occurred in negatively, which is why those need no witness.
                    // The override matters because the search reports a value for every variable index,
                    // including eliminated ones it never assigned, so this variable arrives here already
                    // labelled true and the repair below would then find nothing to do while the unwitnessed
                    // negative clauses stayed violated.
                    set_value(eliminated, false);
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

    void model_reconstructor::drop_internal_only_variables() noexcept
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

    void model_reconstructor::load_values_from_initial_model() noexcept
    {
        std::uint32_t highest {};
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

    void model_reconstructor::set_value(const variable var, const bool value) noexcept
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

    [[nodiscard]] bool model_reconstructor::clause_is_satisfied(const std::span<const literal> clause) const noexcept
    {
        for (const auto lit: clause)
        {
            const auto index = static_cast<std::size_t>(lit.variable_of().index());
            // A variable with no value yet cannot satisfy anything. Treating absent as false made every
            // clause containing a negative literal over an unassigned variable look satisfied, so the
            // elimination replay below skipped clauses it existed to repair.
            if ((index >= present_.size()) || !present_[index])
                continue;
            if (values_[index] != lit.is_negated())
                return true;
        }
        return false;
    }
}
