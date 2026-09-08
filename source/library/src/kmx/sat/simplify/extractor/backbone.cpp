/// @file library/src/kmx/sat/simplify/extractor/backbone.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/extractor/backbone.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/extractor/backbone.hpp>

namespace kmx::sat::simplify::extractor
{
    void backbone::reset() noexcept
    {
        candidates_.clear();
        confirmed_.clear();
        emitted_.clear();
        std::fill(candidate_marks_.begin(), candidate_marks_.end(), 0u);
        std::fill(confirmed_marks_.begin(), confirmed_marks_.end(), 0u);
        std::fill(emitted_marks_.begin(), emitted_marks_.end(), 0u);
        unit_index_valid_ = false;
    }

    void backbone::record_candidate(const literal lit) noexcept
    {
        if (lit.raw() == 0u)
            return;
        if (marked(candidate_marks_, lit.negated()))
            return;
        if (!marked(candidate_marks_, lit))
        {
            mark(candidate_marks_, lit);
            candidates_.push_back(lit);
        }
    }

    void backbone::confirm_candidate(const literal lit) noexcept
    {
        if (!marked(candidate_marks_, lit))
            return;

        if ((database_ != nullptr) && has_unit_clause(lit.negated()))
            return;

        if (marked(confirmed_marks_, lit.negated()))
            return;

        if (!marked(confirmed_marks_, lit))
        {
            mark(confirmed_marks_, lit);
            confirmed_.push_back(lit);
        }
    }

    void backbone::emit_unit_fact(const literal lit) noexcept
    {
        if (!marked(confirmed_marks_, lit))
            return;

        if (marked(emitted_marks_, lit))
            return;

        if ((database_ != nullptr) && !has_unit_clause(lit))
        {
            const auto ref = database_->add_clause(std::array<literal, 1u> {lit}, true);
            if (ref.valid())
            {
                mark(unit_marks_, lit);
                if (clause_sink_)
                    clause_sink_(ref);
                if (proof_manager_ != nullptr)
                    proof_manager_->on_add_derived(ref, std::array<literal, 1u> {lit});
            }
        }

        mark(emitted_marks_, lit);
        emitted_.push_back(lit);
    }

    void backbone::mark(std::vector<std::uint8_t>& marks, const literal lit) noexcept
    {
        const auto slot = static_cast<std::size_t>(lit.raw());
        if (slot >= marks.size())
            marks.resize(slot + 1u, 0u);
        marks[slot] = 1u;
    }

    bool backbone::has_unit_clause(const literal lit) noexcept
    {
        if (database_ == nullptr)
            return false;
        if (!unit_index_valid_)
        {
            std::fill(unit_marks_.begin(), unit_marks_.end(), 0u);
            const auto& storage = database_->storage_of();
            const auto index_unit = [&](const cdcl::clause::ref_t ref) noexcept
            {
                const auto literals = storage.view_literals(ref);
                if (literals.size() == 1u)
                    mark(unit_marks_, literals.front());
            };
            database_->iterate_irredundant(index_unit);
            database_->iterate_redundant(index_unit);
            unit_index_valid_ = true;
        }
        return marked(unit_marks_, lit);
    }
}
