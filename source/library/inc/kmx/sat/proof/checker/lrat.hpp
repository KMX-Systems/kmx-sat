/// @file inc/kmx/sat/proof/checker/lrat.hpp
/// @brief Stricter LRAT validation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/clause/id.hpp>

namespace kmx::sat::proof::checker
{
    /// @brief Stricter LRAT validation.
    ///
    /// Where `checker::online` mirrors events for a cheap real-time sanity check, `checker::lrat` performs the
    /// stronger, format-specific validation LRAT's explicit antecedent chains enable: `check_chain` verifies that
    /// each derived clause's recorded antecedent sequence actually resolves to that clause (in linear time, since
    /// LRAT's antecedents remove the need to search for a RAT witness) by reverse unit propagation - every literal of
    /// the derived clause is assumed false, and the antecedents are replayed in order, each expected to become unit
    /// or falsified under the growing assignment until a conflict is reached; `validate_clause` performs the same
    /// check for one recorded derivation in isolation, useful for incremental/partial replay; `finalize_unsat`
    /// confirms some recorded clause is both empty and backed by a valid antecedent chain, certifying the UNSAT
    /// verdict end to end.
    /// @note This checker requires clause ids to remain stable exactly as `proof::clause::id_allocator` guarantees
    /// while `tracer::lrat` is active; it is the internal counterpart to running an external LRAT checker over the
    /// same proof, used for the proof replay testing requirement. Callers feed it clause content and antecedent
    /// chains via `record_clause`/`record_antecedents` as `proof_event`s arrive; `forget_clause` mirrors deletions.
    class lrat final
    {
    public:
        /// @brief Constructs an LRAT checker with no accumulated verification state.
        /// @throws None (noexcept).
        lrat() noexcept = default;

        /// @brief Records (or replaces) the literal snapshot for a clause identified by its stable proof id.
        /// @param clause_id Stable proof identity of the clause.
        /// @param literals Literal snapshot to associate with `clause_id`.
        /// @throws None (noexcept).
        void record_clause(const clause::id clause_id, const std::span<const literal> literals) noexcept
        {
            if (!clause_id.valid())
            {
                return;
            }
            clauses_[clause_id.value()] = std::vector<literal> {literals.begin(), literals.end()};
        }

        /// @brief Records the ordered LRAT antecedent chain justifying a derived clause.
        /// @param clause_id Stable proof identity of the derived clause.
        /// @param antecedents Ordered antecedent clause ids to replay during chain verification.
        /// @throws None (noexcept).
        void record_antecedents(const clause::id clause_id, const std::span<const clause::id> antecedents) noexcept
        {
            if (!clause_id.valid())
            {
                return;
            }
            auto& chain = antecedents_[clause_id.value()];
            chain.clear();
            chain.reserve(antecedents.size());
            for (const auto& antecedent: antecedents)
            {
                chain.push_back(antecedent.value());
            }
        }

        /// @brief Forgets a clause's recorded literals and antecedent chain once it is permanently deleted.
        /// @param clause_id Stable proof identity of the deleted clause.
        /// @throws None (noexcept).
        void forget_clause(const clause::id clause_id) noexcept
        {
            if (!clause_id.valid())
            {
                return;
            }
            clauses_.erase(clause_id.value());
            antecedents_.erase(clause_id.value());
        }

        /// @brief Checks whether any antecedent chain has been recorded yet.
        /// @return True if at least one derived clause has a recorded antecedent chain.
        /// @throws None (noexcept).
        [[nodiscard]] bool has_recorded() const noexcept { return !antecedents_.empty(); }

        /// @brief Verifies that every recorded derived clause's antecedent chain actually resolves to that clause.
        /// @return True if every recorded antecedent chain is valid; false if none are recorded or any fails.
        /// @throws None (noexcept).
        [[nodiscard]] bool check_chain() const noexcept
        {
            if (antecedents_.empty())
            {
                return false;
            }
            for (const auto& [clause_key, chain]: antecedents_)
            {
                const auto clause_it = clauses_.find(clause_key);
                if (clause_it == clauses_.end() || !verify_chain(clause_it->second, chain))
                {
                    return false;
                }
            }
            return true;
        }

        /// @brief Validates one recorded clause's addition and antecedents in isolation.
        /// @param clause_id Stable proof identity of the clause to validate.
        /// @return True if the clause's recorded antecedent chain resolves to it.
        /// @throws None (noexcept).
        [[nodiscard]] bool validate_clause(const clause::id clause_id) const noexcept
        {
            if (!clause_id.valid())
            {
                return false;
            }
            const auto clause_it = clauses_.find(clause_id.value());
            const auto chain_it = antecedents_.find(clause_id.value());
            if (clause_it == clauses_.end() || chain_it == antecedents_.end())
            {
                return false;
            }
            return verify_chain(clause_it->second, chain_it->second);
        }

        /// @brief Confirms the proof concludes with the empty clause, certifying the UNSAT verdict.
        /// @return True if some recorded clause is empty and backed by a valid antecedent chain.
        /// @throws None (noexcept).
        [[nodiscard]] bool finalize_unsat() const noexcept
        {
            for (const auto& [clause_key, literals]: clauses_)
            {
                if (!literals.empty())
                {
                    continue;
                }
                const auto chain_it = antecedents_.find(clause_key);
                if (chain_it != antecedents_.end() && verify_chain(literals, chain_it->second))
                {
                    return true;
                }
            }
            return false;
        }

    private:
        /// @brief Checks whether `lit` is currently satisfied under `assigned`.
        [[nodiscard]] static bool is_satisfied(const literal lit, const std::unordered_map<variable::index_t, bool>& assigned) noexcept
        {
            const auto entry = assigned.find(lit.variable_of().index());
            return entry != assigned.end() && entry->second != lit.is_negated();
        }

        /// @brief Performs reverse unit propagation: assumes `derived` false and replays `chain` looking for a
        /// conflict, exactly as an LRAT replay checker would.
        [[nodiscard]] bool verify_chain(const std::vector<literal>& derived, const std::vector<clause::id::value_t>& chain) const noexcept
        {
            std::unordered_map<variable::index_t, bool> assigned {};
            for (const auto lit: derived)
            {
                assigned[lit.variable_of().index()] = lit.is_negated();
            }

            for (const auto antecedent_key: chain)
            {
                const auto it = clauses_.find(antecedent_key);
                if (it == clauses_.end())
                {
                    return false;
                }

                bool satisfied {false};
                std::size_t unassigned_count {0};
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
                {
                    return false;
                }
                if (unassigned_count == 0)
                {
                    return true;
                }
                if (unassigned_count > 1)
                {
                    return false;
                }
                assigned[pending.variable_of().index()] = !pending.is_negated();
            }

            return false;
        }

        std::unordered_map<clause::id::value_t, std::vector<literal>> clauses_ {};
        std::unordered_map<clause::id::value_t, std::vector<clause::id::value_t>> antecedents_ {};
    };
}
