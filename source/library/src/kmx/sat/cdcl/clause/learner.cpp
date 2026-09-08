/// @file library/src/kmx/sat/cdcl/clause/learner.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/clause/learner.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/clause/learner.hpp>

namespace kmx::sat::cdcl::clause
{
    ref_t learner::learn_clause(const std::span<const literal> literals) noexcept
    {
        if (literals.empty())
            return {};

        const auto normalized_clause = normalize_clause_literals(literals);
        switch (normalized_clause.size())
        {
            case 0u:
                return {};
            case 1u:
                register_unit(normalized_clause.front());
                return last_learned_ref_;
            case 2u:
                register_binary(normalized_clause[0u], normalized_clause[1u]);
                return last_learned_ref_;
            default:
                return register_large(std::span<const literal> {normalized_clause});
        }
    }

    std::span<const literal> learner::last_learned_clause() const noexcept
    {
        if (learned_clauses_.empty())
            return {};
        return learned_clauses_.back();
    }

    std::size_t learner::clause_count_for_size(const std::size_t size) const noexcept
    {
        std::size_t count {};
        for (const auto& clause: learned_clauses_)
            if (clause.size() == size)
                ++count;
        return count;
    }

    std::vector<literal> learner::normalize_clause_literals(const std::span<const literal> literals) noexcept
    {
        std::vector<literal> normalized {};
        normalized.reserve(literals.size());

        for (const auto lit: literals)
        {
            const auto duplicate_it = std::find_if(normalized.begin(), normalized.end(),
                                                   [lit](const literal existing) noexcept { return existing.raw() == lit.raw(); });
            if (duplicate_it != normalized.end())
                continue;

            const auto opposite_it = std::find_if(
                normalized.begin(), normalized.end(), [lit](const literal existing) noexcept
                { return (existing.variable_of().index() == lit.variable_of().index()) && (existing.is_negated() != lit.is_negated()); });
            if (opposite_it != normalized.end())
                return {};

            normalized.push_back(lit);
        }

        return normalized;
    }

    ref_t learner::append_clause(const std::span<const literal> literals) noexcept
    {
        if (literals.empty())
            return {};

        learned_clauses_.emplace_back(literals.begin(), literals.end());
        return ref_t {next_clause_offset_++};
    }
}
