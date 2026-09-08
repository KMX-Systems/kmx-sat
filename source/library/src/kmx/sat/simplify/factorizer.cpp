/// @file library/src/kmx/sat/simplify/factorizer.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/factorizer.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/factorizer.hpp>

namespace kmx::sat::simplify
{
    void factorizer::run() noexcept
    {
        ++run_count_;
        if (database_ == nullptr)
            return;
        if ((proof_manager_ != nullptr) && proof_manager_->has_active_consumers())
            return;
        if (!build_index())
            return;

        for (std::size_t round = 0u; (round < maximum_rounds) && (effort_ != 0u); ++round)
        {
            collect_candidates();
            bool progress {};
            for (const auto candidate: candidates_)
            {
                if (effort_ == 0u)
                    break;
                progress |= factor_literal(candidate);
            }
            if (!progress)
                break;
        }

        database_->flush_satisfied([this](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
        release_index();
    }

    bool factorizer::build_index() noexcept
    {
        release_index();
        const auto& storage = database_->storage_of();
        variable::index_t highest {};
        // Two passes: the first counts, so every occurrence list is allocated once at its final size instead
        // of growing an element at a time (on an 800-clause formula that was 1,800 reallocations, a tenth of
        // the pass); the second registers, in the same order as before.
        database_->iterate_irredundant(
            [&](const cdcl::clause::ref_t ref) noexcept
            {
                if (!ref.valid() || !storage.is_alive(ref) || database_->is_garbage(ref))
                    return;
                const auto literals = storage.view_literals(ref);
                for (const auto lit: literals)
                    highest = std::max(highest, lit.variable_of().index());
                if ((literals.size() < 2u) || (literals.size() > maximum_clause_size_))
                    return;
                for (const auto lit: literals)
                {
                    ensure_literal_capacity(lit.raw());
                    ++counts_[lit.raw()];
                }
            });
        for (literal::raw_t raw = 0u; raw < occurrences_.size(); ++raw)
        {
            if (counts_[raw] != 0u)
                occurrences_[raw].reserve(counts_[raw]);
            counts_[raw] = 0u;
        }
        database_->iterate_irredundant(
            [&](const cdcl::clause::ref_t ref) noexcept
            {
                if (!ref.valid() || !storage.is_alive(ref) || database_->is_garbage(ref))
                    return;
                const auto literals = storage.view_literals(ref);
                if ((literals.size() < 2u) || (literals.size() > maximum_clause_size_))
                    return;
                register_clause(ref, literals);
            });
        highest_variable_ = std::max(highest, problem_variable_count_);
        ensure_literal_capacity(literal {variable {highest_variable_}, true}.raw());
        effort_ = refs_.size() * effort_per_clause_ + minimum_effort;
        return refs_.size() >= 3u;
    }

    void factorizer::release_index() noexcept
    {
        refs_.clear();
        alive_.clear();
        sizes_.clear();
        for (auto& list: occurrences_)
            list.clear();
        candidates_.clear();
    }

    void factorizer::register_clause(const cdcl::clause::ref_t ref, const std::span<const literal> literals) noexcept
    {
        const auto id = static_cast<clause_id_t>(refs_.size());
        refs_.push_back(ref);
        alive_.push_back(1u);
        sizes_.push_back(static_cast<std::uint32_t>(literals.size()));
        for (const auto lit: literals)
        {
            ensure_literal_capacity(lit.raw());
            occurrences_[lit.raw()].push_back(id);
        }
    }

    void factorizer::ensure_literal_capacity(const literal::raw_t raw) noexcept
    {
        const auto needed = static_cast<std::size_t>(raw) + 2u;
        if (occurrences_.size() < needed)
        {
            occurrences_.resize(needed);
            marks_.resize(needed, 0u);
            selected_marks_.resize(needed, 0u);
            counts_.resize(needed, 0u);
        }
    }

    std::size_t factorizer::alive_occurrences(const literal::raw_t raw) const noexcept
    {
        std::size_t count {};
        for (const auto id: occurrences_[raw])
            count += alive_[id];
        return count;
    }

    void factorizer::collect_candidates() noexcept
    {
        candidates_.clear();
        candidate_counts_.clear();
        for (literal::raw_t raw = 2u; raw < occurrences_.size(); ++raw)
        {
            const auto count = alive_occurrences(raw);
            if (count >= 2u)
            {
                candidates_.push_back(literal {raw});
                candidate_counts_.push_back(count);
            }
        }
        order_.resize(candidates_.size());
        for (std::size_t index = 0u; index < order_.size(); ++index)
            order_[index] = static_cast<std::uint32_t>(index);
        std::sort(order_.begin(), order_.end(),
                  [this](const std::uint32_t left, const std::uint32_t right) noexcept
                  {
                      if (candidate_counts_[left] != candidate_counts_[right])
                          return candidate_counts_[left] > candidate_counts_[right];
                      return candidates_[left].raw() < candidates_[right].raw();
                  });
        ordered_candidates_.resize(candidates_.size());
        for (std::size_t index = 0u; index < order_.size(); ++index)
            ordered_candidates_[index] = candidates_[order_[index]];
        candidates_.swap(ordered_candidates_);
    }

    bool factorizer::factor_literal(const literal anchor) noexcept
    {
        rows_.clear();
        for (const auto id: occurrences_[anchor.raw()])
            if (alive_[id] != 0u)
                rows_.push_back(id);
        if (rows_.size() < 2u)
            return false;

        selected_.clear();
        selected_.push_back(anchor);
        selected_marks_[anchor.raw()] = 1u;
        matched_.assign(rows_.begin(), rows_.end()); // width 1: the anchor's own clause per row

        while (effort_ != 0u)
        {
            const auto best = best_partner(anchor);
            if (best.raw() == 0u)
                break;
            const auto width = selected_.size();
            const auto matched_rows = static_cast<std::size_t>(counts_[best.raw()]);
            const auto saving_now = saving(width, rows_.size());
            const auto saving_next = saving(width + 1u, matched_rows);
            clear_counts();
            if (saving_next <= saving_now)
                break;
            adopt_partner(best);
        }

        for (const auto lit: selected_)
            selected_marks_[lit.raw()] = 0u;
        if ((selected_.size() < 2u) || (saving(selected_.size(), rows_.size()) < 1L))
            return false;
        apply(anchor);
        return true;
    }

    std::int64_t factorizer::saving(const std::size_t literals, const std::size_t bodies) noexcept
    {
        const auto l = static_cast<std::int64_t>(literals);
        const auto b = static_cast<std::int64_t>(bodies);
        return l * b - l - b;
    }

    literal factorizer::best_partner(const literal anchor) noexcept
    {
        matches_.clear();
        touched_.clear();
        const auto& storage = database_->storage_of();
        for (std::uint32_t row = 0u; (row < rows_.size()) && (effort_ != 0u); ++row)
        {
            const auto clause = rows_[row];
            const auto literals = storage.view_literals(refs_[clause]);
            literal pivot {};
            std::size_t pivot_occurrences = std::numeric_limits<std::size_t>::max();
            for (const auto lit: literals)
            {
                if (lit.raw() == anchor.raw())
                    continue;
                marks_[lit.raw()] = 1u;
                if (occurrences_[lit.raw()].size() < pivot_occurrences)
                {
                    pivot_occurrences = occurrences_[lit.raw()].size();
                    pivot = lit;
                }
            }
            const auto size = sizes_[clause];
            for (const auto other: occurrences_[pivot.raw()])
            {
                if (effort_ == 0u)
                    break;
                --effort_;
                if ((other == clause) || (alive_[other] == 0u) || (sizes_[other] != size))
                    continue;
                literal extra {};
                std::uint32_t marked {};
                for (const auto lit: storage.view_literals(refs_[other]))
                    if (marks_[lit.raw()] != 0u)
                        ++marked;
                    else
                        extra = lit;
                if ((marked + 1u != size) || (extra.raw() == 0u))
                    continue;
                if ((extra.raw() == anchor.raw()) || (extra.variable_of().index() == anchor.variable_of().index()))
                    continue;
                if ((selected_marks_[extra.raw()] != 0u) || (marks_[extra.negated().raw()] != 0u))
                    continue;
                if (counts_[extra.raw()]++ == 0u)
                    touched_.push_back(extra);
                matches_.push_back(match {row, extra.raw(), other});
            }
            for (const auto lit: literals)
                marks_[lit.raw()] = 0u;
        }

        literal best {};
        std::uint32_t best_count {};
        for (const auto lit: touched_)
        {
            const auto count = counts_[lit.raw()];
            if ((count > best_count) || (count == best_count && lit.raw() < best.raw()))
            {
                best = lit;
                best_count = count;
            }
        }
        return (best_count >= 2u) ? best : literal {};
    }

    void factorizer::clear_counts() noexcept
    {
        for (const auto lit: touched_)
            counts_[lit.raw()] = 0u;
        touched_.clear();
    }

    void factorizer::adopt_partner(const literal partner) noexcept
    {
        const auto width = selected_.size();
        pending_.assign(rows_.size(), invalid_clause);
        for (const auto& entry: matches_)
            if ((entry.lit == partner.raw()) && (pending_[entry.row] == invalid_clause))
                pending_[entry.row] = entry.clause;

        rows_scratch_.clear();
        matched_scratch_.clear();
        for (std::uint32_t row = 0u; row < rows_.size(); ++row)
        {
            if (pending_[row] == invalid_clause)
                continue;
            rows_scratch_.push_back(rows_[row]);
            for (std::size_t column = 0u; column < width; ++column)
                matched_scratch_.push_back(matched_[row * width + column]);
            matched_scratch_.push_back(pending_[row]);
        }
        rows_.swap(rows_scratch_);
        matched_.swap(matched_scratch_);
        selected_.push_back(partner);
        selected_marks_[partner.raw()] = 1u;
    }

    void factorizer::apply(const literal anchor) noexcept
    {
        const auto& storage = database_->storage_of();
        const variable fresh {++highest_variable_};
        const literal positive {fresh, false};
        const literal negative {fresh, true};
        ensure_literal_capacity(negative.raw());

        if (extension_stack_ != nullptr)
        {
            // The group is recorded with the variable: reconstruction only drops the variable, but the search
            // needs the group to phase the variable consistently with a walk's assignment.
            const auto mark = extension_stack_->witness_mark();
            extension_stack_->append_witness_clause(selected_);
            extension_stack_->push_factor_transformation(fresh, mark);
        }

        // Definitions first: while x is fresh every clause holding ¬x is RAT on ¬x without a resolvent.
        for (const auto lit: selected_)
        {
            const std::array<literal, 2u> definition {negative, lit};
            add_clause(definition);
        }
        // Bodies next: (x ∨ d) is RAT on x, its resolvents with the definitions being the (l ∨ d) still present.
        const auto width = selected_.size();
        for (std::size_t row = 0u; row < rows_.size(); ++row)
        {
            clause_scratch_.clear();
            clause_scratch_.push_back(positive);
            for (const auto lit: storage.view_literals(refs_[rows_[row]]))
                if (lit.raw() != anchor.raw())
                    clause_scratch_.push_back(lit);
            add_clause(clause_scratch_);
        }
        // The rectangle itself is now implied and goes.
        for (std::size_t row = 0u; row < rows_.size(); ++row)
            for (std::size_t column = 0u; column < width; ++column)
                delete_clause(matched_[row * width + column]);

        ++introduced_variable_count_;
        last_introduced_variable_ = fresh;
    }

    void factorizer::add_clause(const std::span<const literal> literals) noexcept
    {
        const auto ref = database_->add_clause(literals, false);
        if (!ref.valid())
            return;
        if (proof_manager_ != nullptr)
            proof_manager_->on_add_derived(ref, literals);
        register_clause(ref, literals);
        ++added_clause_count_;
    }

    void factorizer::delete_clause(const clause_id_t id) noexcept
    {
        if ((id == invalid_clause) || (alive_[id] == 0u))
            return;
        alive_[id] = 0u;
        const auto ref = refs_[id];
        if (proof_manager_ != nullptr)
            proof_manager_->on_delete_clause(ref);
        database_->mark_garbage(ref);
        ++removed_clause_count_;
    }
}
