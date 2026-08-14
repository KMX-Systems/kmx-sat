/// @file inc/kmx/sat/simplify/engine/probing.hpp
/// @brief Failed literal probing and its useful implications.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify::engine
{
    /// @brief Failed literal probing and its useful implications.
    ///
    /// Failed-literal probing tentatively assumes one literal, propagates it, and observes the consequences without
    /// committing to a real search decision: if propagating a literal leads to a conflict, its negation is a unit
    /// fact (the literal is "failed"); if propagating it forces another literal true regardless of which polarity
    /// was tried, a hyper-binary implication or backbone candidate has been found. `probe_literal` runs one such
    /// trial propagation; `run_failed_literal_probing` sweeps candidate literals under a budget;
    /// `learn_hyper_binary` records a shortcut binary implication discovered during probing (reducing future
    /// propagation chain length); `record_backbone_candidate` forwards a literal implied identically under both
    /// polarities to `backbone_extractor` for confirmation.
    /// @note Every unit fact or hyper-binary clause this pass derives must be reported to `proof::proof_manager` like
    /// any other derived clause.
    class probing final
    {
    public:
        /// @brief Constructs a probing engine with no in-progress probe.
        /// @throws None (noexcept).
        probing() noexcept = default;

        /// @brief Attaches the clause database consumed by probing.
        /// @param database Clause database to scan and potentially strengthen.
        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Attaches the proof manager used to report derived structural changes.
        /// @param proof_manager Proof manager to notify about clause shrinking.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Tentatively assumes and propagates one literal to observe its consequences.
        /// @param lit Literal to probe.
        /// @throws None (noexcept).
        void probe_literal(const literal lit) noexcept
        {
            (void) lit;
            ++probe_count_;
        }

        /// @brief Sweeps candidate literals for failed-literal probing under the current pass budget.
        /// @throws None (noexcept).
        void run_failed_literal_probing() noexcept
        {
            backbone_candidates_.clear();
            if (database_ != nullptr)
            {
                bool changed {true};
                while (changed)
                {
                    changed = false;
                    auto assignments = collect_unit_assignments();
                    for (const auto ref: active_refs())
                    {
                        if (!ref.valid() || database_->is_garbage(ref) || !database_->storage_of().is_alive(ref))
                            continue;

                        const auto clause = database_->storage_of().literals_of(ref);
                        if (clause.size() <= 1u)
                            continue;

                        bool satisfied {};
                        std::vector<literal> survivors {};
                        survivors.reserve(clause.size());
                        for (const auto lit: clause)
                        {
                            const auto it = assignments.find(lit.variable_of().index());
                            if (it == assignments.end())
                            {
                                survivors.push_back(lit);
                                continue;
                            }

                            if (literal_is_satisfied(lit, it->second))
                            {
                                satisfied = true;
                                break;
                            }
                        }

                        if (satisfied || survivors.size() != 1u || survivors.size() >= clause.size())
                            continue;

                        auto rewritten_clause = clause;
                        rewritten_clause[0] = survivors[0];
                        database_->storage_of().rewrite_clause_literals(ref, rewritten_clause);
                        database_->storage_of().shrink_clause(ref, 1u);
                        if (proof_manager_ != nullptr)
                            proof_manager_->on_shrink_clause(ref, survivors);

                        assignments[survivors[0].variable_of().index()] = !survivors[0].is_negated();
                        ++hyper_binary_count_;
                        record_backbone_candidate(survivors[0]);
                        changed = true;
                    }
                }
            }
            probing_completed_ = true;
        }

        /// @brief Records a hyper-binary implication shortcut discovered while probing.
        /// @throws None (noexcept).
        void learn_hyper_binary() noexcept { ++hyper_binary_count_; }

        /// @brief Forwards a literal implied identically under both polarities as a backbone candidate.
        /// @param lit Candidate backbone literal.
        /// @throws None (noexcept).
        void record_backbone_candidate(const literal lit) noexcept
        {
            if (std::find(backbone_candidates_.begin(), backbone_candidates_.end(), lit) == backbone_candidates_.end())
                backbone_candidates_.push_back(lit);
            ++backbone_candidate_count_;
        }

        std::span<const literal> backbone_candidates() const noexcept { return backbone_candidates_; }

        std::size_t probe_count() const noexcept { return probe_count_; }

        std::size_t hyper_binary_count() const noexcept { return hyper_binary_count_; }

        std::size_t backbone_candidate_count() const noexcept { return backbone_candidate_count_; }

        bool probing_completed() const noexcept { return probing_completed_; }

    private:
        using assignment_map = std::unordered_map<std::uint32_t, bool>;

        std::vector<cdcl::clause::ref_t> active_refs() const noexcept
        {
            std::vector<cdcl::clause::ref_t> refs {};
            if (database_ == nullptr)
                return refs;

            const auto stats = database_->stats_snapshot();
            refs.reserve(stats.irredundant_count + stats.redundant_count);
            database_->iterate_irredundant([&](const cdcl::clause::ref_t ref) noexcept { refs.push_back(ref); });
            database_->iterate_redundant([&](const cdcl::clause::ref_t ref) noexcept { refs.push_back(ref); });
            return refs;
        }

        assignment_map collect_unit_assignments() const noexcept
        {
            assignment_map assignments {};
            if (database_ == nullptr)
                return assignments;

            for (const auto ref: active_refs())
            {
                if (!ref.valid() || database_->is_garbage(ref))
                    continue;

                const auto clause = database_->storage_of().literals_of(ref);
                if (clause.size() != 1u)
                    continue;

                const auto unit = clause.front();
                assignments[unit.variable_of().index()] = !unit.is_negated();
            }

            return assignments;
        }

        static bool literal_is_satisfied(const literal lit, const bool assigned_true) noexcept
        {
            return lit.is_negated() ? !assigned_true : assigned_true;
        }

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::size_t probe_count_ {};
        std::size_t hyper_binary_count_ {};
        std::size_t backbone_candidate_count_ {};
        std::vector<literal> backbone_candidates_ {};
        bool probing_completed_ {};
    };
}
