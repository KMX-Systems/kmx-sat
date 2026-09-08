/// @file library/src/kmx/sat/simplify/forward_subsumer.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/forward_subsumer.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/forward_subsumer.hpp>

namespace kmx::sat::simplify
{
    void forward_subsumer::run() noexcept
    {
        ++run_count_;
        last_subsumed_ref_ = {};

        if (database_ == nullptr)
            return;

        auto& storage = database_->storage_of();
        clauses_.clear();
        const auto append_clause = [&storage, this](const bool redundant) noexcept
        {
            return [&storage, this, redundant](const cdcl::clause::ref_t ref) noexcept
            {
                const auto literals = storage.view_literals(ref);
                const bool checked = incremental_ && ((storage.header_at(ref).flags & cdcl::bank::subsumption_checked_flag) != 0u);
                clauses_.push_back(indexed_clause {ref, literals, signature_of(literals), true, redundant, checked});
            };
        };
        database_->iterate_irredundant(append_clause(false));
        const auto irredundant_clause_count = clauses_.size();
        database_->iterate_redundant(append_clause(true));

        // Two clauses this pass has already checked against each other, and that have not changed since,
        // cannot newly subsume one another; every pair that matters involves a clause added or rewritten since
        // the previous run (a learned clause, a strengthened one). Such pairs are skipped below, which is what
        // keeps the inprocessing epochs from re-examining the whole formula every two thousand conflicts. The
        // lists scanned, and their order, are exactly the full pass's, so the same subsumptions are found in the
        // same order (the order decides which clauses are promoted first, and that order reaches the watch
        // lists); only the tests the full pass would have rejected are left out.
        build_occurrence_index(full_index_, [](const indexed_clause&) noexcept { return true; });

        for (std::uint32_t left_index {}; left_index < clauses_.size(); ++left_index)
        {
            auto& left = clauses_[left_index];
            if (!left.active || left.literals.empty())
                continue;

            // Any clause `left` subsumes contains every literal of `left`, so it occurs in the rarest of `left`'s
            // occurrence lists; that list is also where a subsumer of `left` is looked for first (the pass that
            // processes the subsumer as `left` finds the pair in any case).
            std::span<const std::uint32_t> rarest_candidates {};
            bool has_rarest {};
            for (const auto lit: left.literals)
            {
                const auto candidates = occurrences_of(full_index_, lit);
                if (candidates.empty())
                {
                    has_rarest = false;
                    break;
                }
                if (!has_rarest || (candidates.size() < rarest_candidates.size()))
                {
                    rarest_candidates = candidates;
                    has_rarest = true;
                }
            }
            if (!has_rarest)
                continue;

            bool left_subsumed {};
            for (const auto candidate_index: rarest_candidates)
            {
                if ((candidate_index == left_index) || !clauses_[candidate_index].active)
                    continue;

                auto& candidate = clauses_[candidate_index];
                if (left.checked && candidate.checked)
                    continue;
                const bool precedes_equal_clause = (candidate_index < left_index) && (candidate.literals.size() == left.literals.size());
                if (((candidate.literals.size() < left.literals.size()) || precedes_equal_clause) &&
                    ((candidate.signature & ~left.signature) == 0u) && clause_subsumes(candidate.literals, left.literals))
                {
                    subsume(left, candidate);
                    left_subsumed = true;
                    break;
                }
            }
            if (left_subsumed)
                continue;

            for (const auto candidate_index: rarest_candidates)
            {
                if ((candidate_index == left_index) || !clauses_[candidate_index].active)
                    continue;

                auto& candidate = clauses_[candidate_index];
                if (left.checked && candidate.checked)
                    continue;
                if ((left.literals.size() <= candidate.literals.size()) && ((left.signature & ~candidate.signature) == 0u) &&
                    clause_subsumes(left.literals, candidate.literals))
                    subsume(candidate, left);
            }
        }

        // Everything still standing has now been checked against everything; strengthening below clears the
        // mark again on the clauses it rewrites, through the storage.
        for (const auto& clause: clauses_)
            if (clause.active)
                storage.header_at(clause.ref).flags |= cdcl::bank::subsumption_checked_flag;

        if (self_subsuming_resolution_enabled_)
            run_self_subsuming_resolution(irredundant_clause_count);

        database_->flush_satisfied([&](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
    }

    void forward_subsumer::run_self_subsuming_resolution(const std::size_t irredundant_clause_count) noexcept
    {
        // Bounded on purpose. Unbounded, this pass is O(clauses x literals x occurrences) and the inprocessing
        // scheduler re-runs it roughly every thirty conflicts over a learned database that grows into the tens
        // of thousands, which measured 14x slower end to end. Only the original clause set is strengthened,
        // only short justifying clauses are considered, and long occurrence lists are skipped.
        if (irredundant_clause_count > max_strengthening_clause_count)
            return;

        auto& storage = database_->storage_of();
        for (std::uint32_t left_index {}; left_index < irredundant_clause_count; ++left_index)
        {
            auto& left = clauses_[left_index];
            if (!left.active || left.literals.empty() || (left.literals.size() > max_justifier_size))
                continue;

            for (const auto pivot: left.literals)
            {
                const auto candidates = occurrences_of(pivot.negated());
                if (candidates.size() > max_strengthening_occurrences)
                    continue;

                for (const auto candidate_index: candidates)
                {
                    if (candidate_index == left_index)
                        continue;

                    auto& candidate = clauses_[candidate_index];
                    if (!candidate.active || candidate.redundant || (candidate.literals.size() <= 1u))
                        continue;
                    if (left.literals.size() > candidate.literals.size())
                        continue;
                    if (!resolves_to_strengthen(left.literals, pivot, candidate.literals))
                        continue;

                    remove_literal(candidate.ref, pivot.negated());

                    // The clause shrank in place, so its cached span and fingerprint are now stale. The
                    // occurrence list for the removed literal keeps naming it, which only ever costs a
                    // failed containment test later.
                    candidate.literals = storage.view_literals(candidate.ref);
                    candidate.signature = signature_of(candidate.literals);
                    if (clause_sink_)
                        clause_sink_(candidate.ref);
                }
            }
        }
    }

    bool forward_subsumer::is_subsumed(const cdcl::clause::ref_t ref) const noexcept
    {
        auto& storage = database_->storage_of();
        if ((database_ == nullptr) || !ref.valid() || !storage.is_alive(ref))
            return false;

        const auto candidate_size = storage.literal_count(ref);
        const auto candidate_clause = storage.view_literals(ref);
        bool subsumed {};
        const auto scan = [&](const cdcl::clause::ref_t other_ref) noexcept
        {
            if (!subsumed)
                subsumed = subsumes_candidate(other_ref, ref, candidate_size, candidate_clause);
        };

        database_->iterate_irredundant(scan);
        database_->iterate_redundant(scan);
        return subsumed;
    }

    bool forward_subsumer::strengthen_by_resolution(const cdcl::clause::ref_t ref, const literal redundant_literal) noexcept
    {
        if ((database_ == nullptr) || !ref.valid())
            return false;

        auto& storage = database_->storage_of();
        if (!storage.is_alive(ref))
            return false;

        const auto target = storage.view_literals(ref);
        if (target.size() <= 1u)
            return false;
        if (std::find_if(target.begin(), target.end(),
                         [redundant_literal](const literal lit) noexcept { return lit.raw() == redundant_literal.raw(); }) == target.end())
            return false;

        bool justified {};
        const auto pivot = redundant_literal.negated();
        database_->iterate_irredundant(
            [&](const cdcl::clause::ref_t candidate_ref) noexcept
            {
                if (justified || (candidate_ref.offset() == ref.offset()) || database_->is_garbage(candidate_ref))
                    return;
                justified = resolves_to_strengthen(storage.view_literals(candidate_ref), pivot, target);
            });

        if (!justified)
            return false;

        remove_literal(ref, redundant_literal);
        return true;
    }

    bool forward_subsumer::resolves_to_strengthen(const std::span<const literal> candidate, const literal pivot,
                                                  const std::span<const literal> target) noexcept
    {
        bool found_pivot {};
        for (const auto lit: candidate)
        {
            if (lit.raw() == pivot.raw())
            {
                found_pivot = true;
                continue;
            }
            const auto present = std::find_if(target.begin(), target.end(), [lit](const literal existing) noexcept
                                              { return existing.raw() == lit.raw(); }) != target.end();
            if (!present)
                return false;
        }
        return found_pivot;
    }

    void forward_subsumer::remove_literal(const cdcl::clause::ref_t ref, const literal redundant_literal) noexcept
    {
        auto& storage = database_->storage_of();
        auto literals = storage.literals_of(ref);
        const auto removed = std::remove_if(literals.begin(), literals.end(), [redundant_literal](const literal lit) noexcept
                                            { return lit.raw() == redundant_literal.raw(); });
        literals.erase(removed, literals.end());
        if (literals.empty())
            return;

        storage.rewrite_clause_literals(ref, std::span<const literal> {literals.data(), literals.size()});
        storage.shrink_clause(ref, static_cast<std::uint32_t>(literals.size()));
        if (proof_manager_ != nullptr)
            proof_manager_->on_shrink_clause(ref, literals);
        ++strengthened_count_;
    }

    std::uint64_t forward_subsumer::signature_of(const std::span<const literal> literals) noexcept
    {
        std::uint64_t signature {};
        for (const auto lit: literals)
            signature |= std::uint64_t {1u} << (lit.raw() & 63u);
        return signature;
    }

    [[nodiscard]] std::span<const std::uint32_t> forward_subsumer::occurrences_of(const occurrence_index& index, const literal lit) noexcept
    {
        const auto slot = static_cast<std::size_t>(lit.raw());
        if (slot + 1u >= index.start.size())
            return {};
        const auto begin = index.start[slot];
        return {index.entries.data() + begin, index.start[slot + 1u] - begin};
    }

    void forward_subsumer::subsume(indexed_clause& subsumed, indexed_clause& subsuming) noexcept
    {
        if (!subsumed.redundant && subsuming.redundant)
        {
            if (!database_->make_irredundant(subsuming.ref))
                return;
            subsuming.redundant = false;
        }
        mark_subsumed(subsumed);
    }

    void forward_subsumer::mark_subsumed(indexed_clause& clause) noexcept
    {
        if (proof_manager_ != nullptr)
            proof_manager_->on_delete_clause(clause.ref);
        database_->mark_garbage(clause.ref);
        clause.active = false;
        last_subsumed_ref_ = clause.ref;
        ++subsumed_count_;
    }

    bool forward_subsumer::subsumes_candidate(const cdcl::clause::ref_t other_ref, const cdcl::clause::ref_t candidate_ref,
                                              const std::uint32_t candidate_size,
                                              const std::span<const literal> candidate_clause) const noexcept
    {
        if (other_ref == candidate_ref)
            return false;

        const auto& storage = database_->storage_of();
        if (storage.literal_count(other_ref) > candidate_size)
            return false;
        return clause_subsumes(storage.view_literals(other_ref), candidate_clause);
    }

    bool forward_subsumer::clause_subsumes(const std::span<const literal> left, const std::span<const literal> right) noexcept
    {
        if (left.empty() || (left.size() > right.size()))
            return false;

        for (const auto left_lit: left)
        {
            const auto found = std::find_if(right.begin(), right.end(),
                                            [left_lit](const literal right_lit) noexcept { return right_lit.raw() == left_lit.raw(); });
            if (found == right.end())
                return false;
        }

        return true;
    }
}
