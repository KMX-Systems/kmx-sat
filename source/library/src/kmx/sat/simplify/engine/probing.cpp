/// @file library/src/kmx/sat/simplify/engine/probing.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/engine/probing.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/engine/probing.hpp>

namespace kmx::sat::simplify::engine
{
    void probing::run_failed_literal_probing() noexcept
    {
        backbone_candidates_.clear();
        std::fill(candidate_marks_.begin(), candidate_marks_.end(), 0u);
        if (database_ != nullptr)
            propagate_units_to_fixpoint();
        probing_completed_ = true;
    }

    void probing::record_backbone_candidate(const literal lit) noexcept
    {
        const auto slot = static_cast<std::size_t>(lit.raw());
        if (slot >= candidate_marks_.size())
            candidate_marks_.resize(slot + 1u, 0u);
        if (candidate_marks_[slot] == 0u)
        {
            candidate_marks_[slot] = 1u;
            backbone_candidates_.push_back(lit);
        }
        ++backbone_candidate_count_;
    }

    void probing::propagate_units_to_fixpoint() noexcept
    {
        auto& storage = database_->storage_of();
        refs_.clear();
        literal::raw_t highest_raw {};
        const auto collect = [&](const cdcl::clause::ref_t ref) noexcept
        {
            if (!ref.valid() || !storage.is_alive(ref))
                return;
            for (const auto lit: storage.view_literals(ref))
                highest_raw = std::max(highest_raw, lit.raw());
            refs_.push_back(ref);
        };
        database_->iterate_irredundant(collect);
        database_->iterate_redundant(collect);
        if (refs_.empty())
            return;

        const auto literal_slots = static_cast<std::size_t>(highest_raw) + 2u;
        values_.assign(literal_slots, 0);
        queue_.clear();

        // Units first: they seed the propagation. Contradicting units are left for the search to refute.
        for (const auto ref: refs_)
        {
            const auto literals = storage.view_literals(ref);
            if (literals.size() == 1u)
                assign_if_unassigned(literals.front());
        }

        // Occurrence index over the non-unit clauses, built with a counting pass.
        occurrence_starts_.assign(literal_slots + 1u, 0u);
        for (const auto ref: refs_)
        {
            const auto literals = storage.view_literals(ref);
            if (literals.size() < 2u)
                continue;
            for (const auto lit: literals)
                ++occurrence_starts_[lit.raw() + 1u];
        }
        for (std::size_t index = 1u; index < occurrence_starts_.size(); ++index)
            occurrence_starts_[index] += occurrence_starts_[index - 1u];
        occurrences_.assign(occurrence_starts_.back(), 0u);
        occurrence_fill_.assign(occurrence_starts_.begin(), occurrence_starts_.end() - 1L);
        // Every clause starts with all its literals counted as unassigned; the seed units are then applied by
        // the closure below like any derived literal, so each assignment is accounted for exactly once.
        unassigned_counts_.assign(refs_.size(), 0u);
        satisfied_.assign(refs_.size(), 0u);
        for (std::uint32_t index = 0u; index < refs_.size(); ++index)
        {
            const auto literals = storage.view_literals(refs_[index]);
            if (literals.size() < 2u)
                continue;
            for (const auto lit: literals)
                occurrences_[occurrence_fill_[lit.raw()]++] = index;
            unassigned_counts_[index] = static_cast<std::uint32_t>(literals.size());
        }

        // Closure: a literal made true satisfies the clauses holding it and falsifies its negation elsewhere.
        for (std::size_t head = 0u; head < queue_.size(); ++head)
        {
            const auto lit = queue_[head];
            for (auto position = occurrence_starts_[lit.raw()]; position < occurrence_starts_[lit.raw() + 1u]; ++position)
                satisfied_[occurrences_[position]] = 1u;
            const auto falsified = lit.negated().raw();
            for (auto position = occurrence_starts_[falsified]; position < occurrence_starts_[falsified + 1u]; ++position)
            {
                const auto index = occurrences_[position];
                if ((satisfied_[index] != 0u) || (unassigned_counts_[index] == 0u))
                    continue;
                if (--unassigned_counts_[index] == 1u)
                    strengthen_to_unit(index);
            }
        }
    }

    void probing::strengthen_to_unit(const std::uint32_t index) noexcept
    {
        auto& storage = database_->storage_of();
        const auto ref = refs_[index];
        const auto literals = storage.view_literals(ref);
        literal survivor {};
        for (const auto lit: literals)
            if (values_[lit.raw()] == 0)
                survivor = lit;
        if (survivor.raw() == 0u)
            return;

        const std::array<literal, 1u> unit {survivor};
        storage.rewrite_clause_literals(ref, unit);
        storage.shrink_clause(ref, 1u);
        if (proof_manager_ != nullptr)
            proof_manager_->on_shrink_clause(ref, unit);
        satisfied_[index] = 1u;
        ++hyper_binary_count_;
        record_backbone_candidate(survivor);
        assign_if_unassigned(survivor);
    }

    void probing::assign_if_unassigned(const literal lit) noexcept
    {
        if ((values_[lit.raw()] != 0) || (values_[lit.negated().raw()] != 0))
            return;
        values_[lit.raw()] = 1;
        values_[lit.negated().raw()] = -1;
        queue_.push_back(lit);
    }
}
