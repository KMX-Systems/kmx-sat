/// @file library/src/kmx/sat/cdcl/witness_checker.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/witness_checker.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/witness_checker.hpp>

namespace kmx::sat::cdcl
{
    bool witness_checker::check_model_against_original(const model_view model) const noexcept
    {
        if (clauses_ == nullptr)
            return true;

        bool satisfied = true;
        clauses_->iterate_irredundant(
            [&](const clause::ref_t ref) noexcept
            {
                if (!satisfied)
                    return;
                satisfied = clause_satisfied(ref, model);
            });
        return satisfied;
    }

    bool witness_checker::check_model_against_current(const model_view model) const noexcept
    {
        if (clauses_ == nullptr)
            return true;

        bool satisfied = true;
        clauses_->iterate_irredundant(
            [&](const clause::ref_t ref) noexcept
            {
                if (!satisfied)
                    return;
                satisfied = clause_satisfied(ref, model);
            });
        clauses_->iterate_redundant(
            [&](const clause::ref_t ref) noexcept
            {
                if (!satisfied)
                    return;
                satisfied = clause_satisfied(ref, model);
            });
        return satisfied;
    }

    bool witness_checker::check_constraint_satisfaction(const model_view model) const noexcept
    {
        if ((constraint_ == nullptr) || !constraint_->has_constraint_clause())
            return true;

        const auto clause = constraint_->constraint_clause_ref();
        if (!clause.has_value())
            return true;

        bool satisfied {};
        for (const auto lit: *clause)
            if (literal_satisfied(lit, model))
            {
                satisfied = true;
                break;
            }

        return satisfied;
    }

    bool witness_checker::literal_satisfied(const literal lit, const model_view model) noexcept
    {
        for (const auto value: model.values())
            if (value.variable_of() == lit.variable_of())
                return value.is_negated() == lit.is_negated();
        return false;
    }

    bool witness_checker::clause_satisfied(const clause::ref_t ref, const model_view model) const noexcept
    {
        if (!ref.valid())
            return true;

        const auto literals = clauses_->storage_of().view_literals(ref);
        for (const auto lit: literals)
            if (literal_satisfied(lit, model))
                return true;
        return false;
    }
}
