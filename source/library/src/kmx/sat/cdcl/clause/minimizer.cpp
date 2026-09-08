/// @file library/src/kmx/sat/cdcl/clause/minimizer.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/clause/minimizer.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/clause/minimizer.hpp>

namespace kmx::sat::cdcl::clause
{
    void minimizer::minimize_learned_clause(const ref_t ref) noexcept
    {
        if ((storage_ == nullptr) || !ref.valid()) [[unlikely]]
            return;

        const auto literals = storage_->view_literals(ref);
        if (literals.size() <= 1u)
            return;

        distinct_scratch_.clear();
        for (const auto lit: literals)
            if (std::find(distinct_scratch_.begin(), distinct_scratch_.end(), lit.raw()) == distinct_scratch_.end())
                distinct_scratch_.push_back(lit.raw());

        if (!distinct_scratch_.empty())
        {
            target_sizes_[ref.offset()] = static_cast<std::uint32_t>(distinct_scratch_.size());
            minimized_.insert(ref.offset());
        }
    }

    void minimizer::minimize_learned_clause(const ref_t ref, const level_lookup_t level_of, const reason_lookup_t reason_of,
                                            const void* context) noexcept
    {
        if ((storage_ == nullptr) || !ref.valid() || (level_of == nullptr) || (reason_of == nullptr))
        {
            minimize_learned_clause(ref);
            return;
        }

        const auto literals = storage_->view_literals(ref);
        if (literals.size() <= 1u)
            return;

        begin_minimization_scan();
        for (const auto lit: literals)
            mark_exact_literal(lit.raw());

        reason_stack_.clear();
        const auto can_remove = [&](const auto& self, const variable var) noexcept -> bool
        {
            const auto variable_index = var.index();
            if (is_known_removable(variable_index))
                return true;
            if (!begin_visit(variable_index))
                return false;

            const auto reason_view = reason_of(context, var);
            if (reason_view.empty())
            {
                end_visit(variable_index);
                return false;
            }

            // `reason_of` hands back a span into one shared scratch buffer that the next recursive call
            // overwrites, so this level's literals must be held somewhere the recursion owns. They go on a
            // single stack that is truncated on the way out, rather than into a per-level vector: this
            // recursion runs on every learned clause, and a heap allocation per level made the allocator one
            // of the largest single costs in the profile.
            const auto base = reason_stack_.size();
            reason_stack_.insert(reason_stack_.end(), reason_view.begin(), reason_view.end());
            const auto count = reason_stack_.size() - base;

            bool removable = true;
            for (std::size_t offset = 0u; offset < count; ++offset)
            {
                // Re-indexed rather than held by reference: a deeper call can grow the stack and move it.
                const auto reason_literal = reason_stack_[base + offset];
                if ((reason_literal.variable_of().index() == variable_index) || (level_of(context, reason_literal.variable_of()) == 0u) ||
                    is_exact_literal(reason_literal.raw()))
                    continue;

                if (!self(self, reason_literal.variable_of()))
                {
                    removable = false;
                    break;
                }
            }

            reason_stack_.resize(base);
            end_visit(variable_index);
            if (removable)
                mark_removable(variable_index);
            return removable;
        };

        auto& minimized_literals = minimized_literals_scratch_;
        minimized_literals.clear();
        minimized_literals.push_back(literals.front());
        for (std::size_t index = 1u; index < literals.size(); ++index)
        {
            const auto lit = literals[index];
            if (!can_remove(can_remove, lit.variable_of()))
                minimized_literals.push_back(lit);
        }

        if (minimized_literals.size() < literals.size())
        {
            storage_->rewrite_clause_literals(ref, minimized_literals);
            target_sizes_[ref.offset()] = static_cast<std::uint32_t>(minimized_literals.size());
            minimized_.insert(ref.offset());
        }
        else
        {
            minimize_learned_clause(ref);
        }
    }

    void minimizer::shrink_clause(const ref_t ref) noexcept
    {
        if ((storage_ == nullptr) || !ref.valid()) [[unlikely]]
            return;

        if (minimized_.find(ref.offset()) != minimized_.end())
        {
            const auto target_size = target_sizes_.contains(ref.offset()) ? target_sizes_.at(ref.offset()) : 1u;
            storage_->shrink_clause(ref, target_size);
            shrunk_.insert(ref.offset());
        }
    }

    void minimizer::recompute_glue(const ref_t ref) noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return;
        const auto target_size = target_sizes_.contains(ref.offset()) ? target_sizes_.at(ref.offset()) : 1u;
        last_glue_ = std::max<std::uint32_t>(1u, target_size);
        glue_.insert_or_assign(ref.offset(), last_glue_);
        if (database_ != nullptr)
            database_->set_glue(ref, last_glue_);
    }

    void minimizer::recompute_glue(const ref_t ref, const level_lookup_t level_of, const void* context) noexcept
    {
        if ((storage_ == nullptr) || !ref.valid() || (level_of == nullptr)) [[unlikely]]
            return;

        const auto literals = storage_->view_literals(ref);
        distinct_scratch_.clear();
        for (const auto lit: literals)
        {
            const auto level = level_of(context, lit.variable_of());
            if (std::find(distinct_scratch_.begin(), distinct_scratch_.end(), level) == distinct_scratch_.end())
                distinct_scratch_.push_back(level);
        }

        last_glue_ = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(distinct_scratch_.size()));
        glue_.insert_or_assign(ref.offset(), last_glue_);
        if (database_ != nullptr)
            database_->set_glue(ref, last_glue_);
    }

    void minimizer::promote_if_needed(const ref_t ref) noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return;
        if (glue_.contains(ref.offset()) && (glue_.at(ref.offset()) <= 2u))
        {
            promoted_.insert(ref.offset());
            if (database_ != nullptr)
                database_->promote_clause(ref);
        }
    }

    void minimizer::begin_minimization_scan() noexcept
    {
        if (++minimization_stamp_ == 0u)
        {
            std::fill(exact_literal_stamps_.begin(), exact_literal_stamps_.end(), 0u);
            std::fill(removable_stamps_.begin(), removable_stamps_.end(), 0u);
            std::fill(visiting_stamps_.begin(), visiting_stamps_.end(), 0u);
            minimization_stamp_ = 1u;
        }
    }

    void minimizer::stamp_at(std::vector<std::uint32_t>& stamps, const std::size_t index, const std::uint32_t stamp) noexcept
    {
        if (index >= stamps.size())
            stamps.resize(index + 1u, 0u);
        stamps[index] = stamp;
    }

    [[nodiscard]] bool minimizer::begin_visit(const std::uint32_t variable_index) noexcept
    {
        if (has_stamp(visiting_stamps_, static_cast<std::size_t>(variable_index), minimization_stamp_))
            return false;
        stamp_at(visiting_stamps_, static_cast<std::size_t>(variable_index), minimization_stamp_);
        return true;
    }
}
