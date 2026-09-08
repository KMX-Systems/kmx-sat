/// @file library/src/kmx/sat/simplify/eliminator/variable/bounded.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/eliminator/variable/bounded.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>

namespace kmx::sat::simplify::eliminator::variable
{
    [[nodiscard]] bool bounded::is_frozen(const kmx::sat::variable var) const noexcept
    {
        for (const auto frozen: frozen_)
            if (frozen.index() == var.index())
                return true;
        return false;
    }

    void bounded::run() noexcept
    {
        eliminated_variables_.clear();
        if (database_ == nullptr)
            return;

        build_occurrences();

        for (std::uint32_t index = 1u; index < occurrences_.size(); ++index)
        {
            const kmx::sat::variable candidate {index};
            if (!can_eliminate(candidate))
                continue;
            if (!build_resolvents(candidate))
                continue;
            apply_elimination(candidate);
        }

        // Compaction is deferred to the end of the pass: `flush_satisfied` relocates clauses, which would
        // dangle the references still held in the occurrence lists of variables not yet considered.
        if (elimination_count_ != 0u)
            database_->flush_satisfied([this](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
    }

    std::int64_t bounded::score_variable(const kmx::sat::variable var) const noexcept
    {
        if (database_ == nullptr)
            return 1L;

        const auto index = static_cast<std::size_t>(var.index());
        if (index >= occurrences_.size())
            return 1L;

        const auto& entry = occurrences_[index];
        const auto removed = static_cast<std::int64_t>(entry.positive.size() + entry.negative.size());
        const auto produced = static_cast<std::int64_t>(entry.positive.size() * entry.negative.size());
        return produced - removed;
    }

    bool bounded::can_eliminate(const kmx::sat::variable var) const noexcept
    {
        if (database_ == nullptr)
            return false;

        const auto index = static_cast<std::size_t>(var.index());
        if (index >= occurrences_.size())
            return false;

        if (is_frozen(var))
            return false;

        const auto& entry = occurrences_[index];
        if (entry.positive.empty() && entry.negative.empty())
            return false;

        return score_variable(var) <= clause_growth_limit_;
    }

    bool bounded::build_resolvents(const kmx::sat::variable var) noexcept
    {
        resolvents_.clear();
        if (database_ == nullptr)
            return false;

        const auto index = static_cast<std::size_t>(var.index());
        if (index >= occurrences_.size())
            return false;

        const auto& entry = occurrences_[index];
        const auto& storage = database_->storage_of();

        for (const auto positive_ref: entry.positive)
        {
            if (database_->is_garbage(positive_ref))
                continue;
            for (const auto negative_ref: entry.negative)
            {
                if (database_->is_garbage(negative_ref))
                    continue;

                resolvent_scratch_.clear();
                if (!resolve(storage.view_literals(positive_ref), storage.view_literals(negative_ref), var))
                    continue;
                if (resolvent_scratch_.size() > resolvent_width_limit_)
                    return false;

                resolvents_.push_back(resolvent_scratch_);
                if (static_cast<std::int64_t>(resolvents_.size()) >
                    static_cast<std::int64_t>(entry.positive.size() + entry.negative.size()) + clause_growth_limit_)
                    return false;
            }
        }

        // An empty resolvent means the formula is unsatisfiable; that is a conclusion for the search to
        // reach through normal propagation, not something this pass should encode by deleting clauses.
        return std::none_of(resolvents_.begin(), resolvents_.end(),
                            [](const std::vector<literal>& clause) noexcept { return clause.empty(); });
    }

    void bounded::apply_elimination(const kmx::sat::variable var) noexcept
    {
        if (database_ == nullptr)
            return;

        const auto index = static_cast<std::size_t>(var.index());
        if (index >= occurrences_.size())
            return;

        emit_extension_record(var);

        auto& entry = occurrences_[index];
        for (const auto ref: entry.positive)
            delete_clause(ref);
        for (const auto ref: entry.negative)
            delete_clause(ref);

        for (const auto& resolvent: resolvents_)
        {
            const auto ref = database_->add_clause(std::span<const literal> {resolvent}, false);
            if (proof_manager_ != nullptr)
                proof_manager_->on_add_original(ref, std::span<const literal> {resolvent});
            if (clause_sink_)
                clause_sink_(ref);
            register_occurrence(ref, resolvent);
        }

        entry.positive.clear();
        entry.negative.clear();
        eliminated_variables_.push_back(var);
        ++elimination_count_;
    }

    void bounded::emit_extension_record(const kmx::sat::variable var) noexcept
    {
        if ((extension_stack_ == nullptr) || (database_ == nullptr))
            return;

        const auto index = static_cast<std::size_t>(var.index());
        if (index >= occurrences_.size())
            return;

        const auto& entry = occurrences_[index];
        const auto& storage = database_->storage_of();
        const auto mark = extension_stack_->witness_mark();
        // Only the clauses containing the variable positively are stored, and that is not an economy: it is
        // what makes reconstruction correct. Setting the variable false satisfies every clause containing it
        // negatively outright, so those need no witness; the only question left is whether some clause
        // containing it positively is otherwise unsatisfied, in which case setting it true rescues that clause
        // without endangering the negative ones -- the resolvents added in its place guarantee a positive and
        // a negative clause cannot both be otherwise unsatisfied. Storing both sides instead leaves the replay
        // flipping the variable back and forth per clause and settling on whichever came last, which is how a
        // reconstructed "model" ends up falsifying a clause of the original formula.
        for (const auto ref: entry.positive)
            if (!database_->is_garbage(ref))
                extension_stack_->append_witness_clause(storage.view_literals(ref));
        extension_stack_->push_bve_elimination(var, mark);
    }

    bool bounded::resolve(const std::span<const literal> positive, const std::span<const literal> negative,
                          const kmx::sat::variable var) noexcept
    {
        const auto append = [this, var](const std::span<const literal> source) noexcept
        {
            for (const auto lit: source)
            {
                if (lit.variable_of().index() == var.index())
                    continue;
                const auto duplicate = std::find_if(resolvent_scratch_.begin(), resolvent_scratch_.end(),
                                                    [lit](const literal existing) noexcept { return existing.raw() == lit.raw(); });
                if (duplicate == resolvent_scratch_.end())
                    resolvent_scratch_.push_back(lit);
            }
        };

        append(positive);
        const auto boundary = resolvent_scratch_.size();
        append(negative);

        for (std::size_t left = 0u; left < boundary; ++left)
            for (std::size_t right = boundary; right < resolvent_scratch_.size(); ++right)
                if (resolvent_scratch_[left].raw() == resolvent_scratch_[right].negated().raw())
                    return false;

        return true;
    }

    void bounded::delete_clause(const cdcl::clause::ref_t ref) noexcept
    {
        if (database_->is_garbage(ref))
            return;
        if (proof_manager_ != nullptr)
            proof_manager_->on_delete_clause(ref);
        database_->mark_garbage(ref);
    }

    void bounded::register_occurrence(const cdcl::clause::ref_t ref, const std::span<const literal> literals) noexcept
    {
        for (const auto lit: literals)
        {
            const auto index = static_cast<std::size_t>(lit.variable_of().index());
            if (index >= occurrences_.size())
                occurrences_.resize(index + 1u);
            if (lit.is_negated())
                occurrences_[index].negative.push_back(ref);
            else
                occurrences_[index].positive.push_back(ref);
        }
    }

    void bounded::build_occurrences() noexcept
    {
        occurrences_.clear();
        const auto& storage = database_->storage_of();
        database_->iterate_irredundant(
            [this, &storage](const cdcl::clause::ref_t ref) noexcept
            {
                if (!database_->is_garbage(ref))
                    register_occurrence(ref, storage.view_literals(ref));
            });
    }
}
