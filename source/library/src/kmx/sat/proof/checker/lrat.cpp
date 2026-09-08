/// @file library/src/kmx/sat/proof/checker/lrat.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/checker/lrat.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/checker/lrat.hpp>

namespace kmx::sat::proof::checker
{
    void lrat::record_clause(const clause::id clause_id, const std::span<const literal> literals) noexcept
    {
        if (!clause_id.valid())
            return;
        clauses_[clause_id.value()] = std::vector<literal> {literals.begin(), literals.end()};
    }

    void lrat::record_antecedents(const clause::id clause_id, const std::span<const clause::id> antecedents) noexcept
    {
        if (!clause_id.valid())
            return;
        auto& chain = antecedents_[clause_id.value()];
        chain.clear();
        chain.reserve(antecedents.size());
        for (const auto& antecedent: antecedents)
            chain.push_back(antecedent.value());
    }

    void lrat::forget_clause(const clause::id clause_id) noexcept
    {
        if (!clause_id.valid())
            return;
        clauses_.erase(clause_id.value());
        antecedents_.erase(clause_id.value());
    }

    [[nodiscard]] bool lrat::check_chain() const noexcept
    {
        if (antecedents_.empty())
            return false;
        for (const auto& [clause_key, chain]: antecedents_)
        {
            const auto clause_it = clauses_.find(clause_key);
            if ((clause_it == clauses_.end()) || !verify_chain(clause_it->second, chain))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool lrat::validate_clause(const clause::id clause_id) const noexcept
    {
        if (!clause_id.valid())
            return false;
        const auto clause_it = clauses_.find(clause_id.value());
        const auto chain_it = antecedents_.find(clause_id.value());
        if ((clause_it == clauses_.end()) || (chain_it == antecedents_.end()))
            return false;
        return verify_chain(clause_it->second, chain_it->second);
    }

    [[nodiscard]] bool lrat::finalize_unsat() const noexcept
    {
        for (const auto& [clause_key, literals]: clauses_)
        {
            if (!literals.empty())
                continue;
            const auto chain_it = antecedents_.find(clause_key);
            if ((chain_it != antecedents_.end()) && verify_chain(literals, chain_it->second))
                return true;
        }
        return false;
    }

    [[nodiscard]] bool lrat::verify_chain(const std::vector<literal>& derived, const std::vector<clause::id::value_t>& chain) const noexcept
    {
        std::unordered_map<variable::index_t, bool> assigned {};
        for (const auto lit: derived)
            assigned[lit.variable_of().index()] = lit.is_negated();

        for (const auto antecedent_key: chain)
        {
            const auto it = clauses_.find(antecedent_key);
            if (it == clauses_.end())
                return false;

            bool satisfied {};
            std::size_t unassigned_count {};
            literal pending {};
            for (const auto lit: it->second)
            {
                const auto entry = assigned.find(lit.variable_of().index());
                if (entry == assigned.end())
                {
                    ++unassigned_count;
                    pending = lit;
                    continue;
                }
                if (entry->second != lit.is_negated())
                {
                    satisfied = true;
                    break;
                }
            }

            if (satisfied)
                return false;
            if (unassigned_count == 0u)
                return true;
            if (unassigned_count > 1u)
                return false;
            assigned[pending.variable_of().index()] = !pending.is_negated();
        }

        return false;
    }
}
