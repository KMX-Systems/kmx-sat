/// @file library/src/kmx/sat/cdcl/conflict_analyzer.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/conflict_analyzer.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/conflict_analyzer.hpp>

namespace kmx::sat::cdcl
{
    void conflict_analyzer::analyze() noexcept
    {
        resolution_chain_literals_.clear();
        resolution_chain_step_count_ = 0u;

        if (pending_conflict_literals_.empty())
            learned_literals_.clear();
        else
        {
            learned_literals_ = std::move(pending_conflict_literals_);
            pending_conflict_literals_.clear();
        }

        // Keep first-occurrence order while removing duplicates by variable identity.
        unique_scratch_.clear();
        unique_scratch_.reserve(learned_literals_.size());
        for (const auto lit: learned_literals_)
        {
            const auto duplicate_it = std::find_if(unique_scratch_.begin(), unique_scratch_.end(), [lit](const literal existing) noexcept
                                                   { return existing.variable_of().index() == lit.variable_of().index(); });
            if (duplicate_it == unique_scratch_.end())
                unique_scratch_.push_back(lit);
        }
        learned_literals_.assign(unique_scratch_.begin(), unique_scratch_.end());

        // Conservative minimization: keep asserting literal and drop level-0 tail literals.
        if (learned_literals_.size() > 1u)
        {
            minimized_scratch_.clear();
            minimized_scratch_.reserve(learned_literals_.size());
            minimized_scratch_.push_back(learned_literals_.front());

            for (std::size_t index = 1u; index < learned_literals_.size(); ++index)
            {
                const auto lit = learned_literals_[index];
                if (level_of_variable(lit.variable_of()) == 0u)
                    continue;
                minimized_scratch_.push_back(lit);
            }

            learned_literals_.assign(minimized_scratch_.begin(), minimized_scratch_.end());
        }

        // Canonicalize asserting literal position: keep the highest-level literal at index 0.
        if (learned_literals_.size() > 1u)
        {
            std::size_t asserting_index {};
            auto asserting_level = level_of_variable(learned_literals_.front().variable_of());

            for (std::size_t index = 1u; index < learned_literals_.size(); ++index)
            {
                const auto candidate_level = level_of_variable(learned_literals_[index].variable_of());
                if (candidate_level > asserting_level)
                {
                    asserting_level = candidate_level;
                    asserting_index = index;
                }
            }

            if (asserting_index != 0u)
            {
                const auto asserting_literal = learned_literals_[asserting_index];
                learned_literals_.erase(learned_literals_.begin() + static_cast<std::ptrdiff_t>(asserting_index));
                learned_literals_.insert(learned_literals_.begin(), asserting_literal);
            }
        }

        bump_candidates_.clear();
        bump_candidates_.reserve(learned_literals_.size());
        for (const auto lit: learned_literals_)
        {
            const auto var = lit.variable_of();
            const auto seen_it = std::find_if(bump_candidates_.begin(), bump_candidates_.end(),
                                              [var](const variable existing) noexcept { return existing.index() == var.index(); });
            if (seen_it == bump_candidates_.end())
                bump_candidates_.push_back(var);
        }

        backjump_level_ = second_highest_decision_level();
    }

    void conflict_analyzer::analyze_via_resolution(const std::span<const literal> trail_in_order, const std::uint32_t current_level,
                                                   const level_lookup_t level_of, const reason_lookup_t reason_of,
                                                   const void* context) noexcept
    {
        resolution_chain_literals_.clear();
        resolution_chain_step_count_ = 0u;
        learned_literals_.clear();
        bump_candidates_.clear();
        backjump_level_ = 0u;

        conflict_scratch_.clear();
        if (!pending_conflict_literals_.empty())
        {
            conflict_scratch_.assign(pending_conflict_literals_.begin(), pending_conflict_literals_.end());
            pending_conflict_literals_.clear();
        }

        if (conflict_scratch_.empty() || (level_of == nullptr) || (reason_of == nullptr))
            return;

        for (const auto index: touched_variable_indices_)
            if (index < seen_flags_.size())
                seen_flags_[index] = 0u;
        touched_variable_indices_.clear();

        const auto mark_seen = [this](const variable var) noexcept -> bool
        {
            const auto index = static_cast<std::size_t>(var.index());
            if (index >= seen_flags_.size())
                seen_flags_.resize(index + 1u, 0u);
            if (seen_flags_[index] != 0u)
                return false;
            seen_flags_[index] = 1u;
            touched_variable_indices_.push_back(index);
            return true;
        };

        const auto is_seen = [this](const variable var) noexcept -> bool
        {
            const auto index = static_cast<std::size_t>(var.index());
            return (index < seen_flags_.size()) && (seen_flags_[index] != 0u);
        };

        tail_scratch_.clear();
        std::uint32_t unresolved_at_current_level {};
        bool have_pivot {};
        literal pivot {};
        std::span<const literal> literals_to_resolve = conflict_scratch_;
        auto trail_cursor = static_cast<std::ptrdiff_t>(trail_in_order.size()) - 1L;

        for (;;)
        {
            for (const auto candidate: literals_to_resolve)
            {
                const auto candidate_variable = candidate.variable_of();
                if (have_pivot && (candidate_variable.index() == pivot.variable_of().index()))
                    continue;
                if (!mark_seen(candidate_variable))
                    continue;

                bump_candidates_.push_back(candidate_variable);
                const auto candidate_level = level_of(context, candidate_variable);
                if (candidate_level == 0u)
                    continue;
                if (candidate_level >= current_level)
                    ++unresolved_at_current_level;
                else
                    tail_scratch_.push_back(candidate);
            }

            if (unresolved_at_current_level == 0u)
            {
                // Nothing left to resolve at the conflict's decision level: either every literal was a
                // root-forced (level-0) fact, or the previous resolution round already discharged the last
                // one. There is no genuine UIP to find; stop before the pivot search below would otherwise
                // underflow the unsigned counter.
                break;
            }

            have_pivot = false;
            pivot = {};
            while (trail_cursor >= 0L)
            {
                const auto trail_literal = trail_in_order[static_cast<std::size_t>(trail_cursor)];
                --trail_cursor;
                if (is_seen(trail_literal.variable_of()) && (level_of(context, trail_literal.variable_of()) == current_level))
                {
                    pivot = trail_literal;
                    have_pivot = true;
                    break;
                }
            }

            if (!have_pivot)
                break;

            --unresolved_at_current_level;
            if (unresolved_at_current_level == 0u)
                break;

            literals_to_resolve = reason_of(context, pivot.variable_of());
        }

        if (have_pivot)
            learned_literals_.push_back(pivot.negated());
        else if (!tail_scratch_.empty())
        {
            learned_literals_.push_back(tail_scratch_.front());
            tail_scratch_.erase(tail_scratch_.begin());
        }
        else
        {
            return;
        }

        std::uint32_t highest_tail_level {};
        for (const auto lit: tail_scratch_)
        {
            const auto level = level_of(context, lit.variable_of());
            if (level > highest_tail_level)
                highest_tail_level = level;
        }
        backjump_level_ = highest_tail_level;

        std::sort(tail_scratch_.begin(), tail_scratch_.end(),
                  [level_of, context](const literal lhs, const literal rhs) noexcept
                  {
                      const auto lhs_level = level_of(context, lhs.variable_of());
                      const auto rhs_level = level_of(context, rhs.variable_of());
                      if (lhs_level != rhs_level)
                          return lhs_level > rhs_level;
                      return lhs.raw() < rhs.raw();
                  });

        learned_literals_.insert(learned_literals_.end(), tail_scratch_.begin(), tail_scratch_.end());

        canonical_scratch_.clear();
        canonical_scratch_.reserve(learned_literals_.size());
        for (const auto lit: learned_literals_)
        {
            const auto duplicate_it =
                std::find_if(canonical_scratch_.begin(), canonical_scratch_.end(), [lit](const literal existing) noexcept
                             { return existing.variable_of().index() == lit.variable_of().index(); });
            if (duplicate_it == canonical_scratch_.end())
                canonical_scratch_.push_back(lit);
        }
        learned_literals_.assign(canonical_scratch_.begin(), canonical_scratch_.end());
    }

    literal conflict_analyzer::derive_first_uip() const noexcept
    {
        if (learned_literals_.empty())
            return {};
        return learned_literals_.front();
    }

    void conflict_analyzer::build_resolution_chain() noexcept
    {
        resolution_chain_step_count_ = learned_literals_.empty() ? 0u : static_cast<std::uint32_t>(learned_literals_.size() - 1u);
        resolution_chain_literals_.clear();
        if (learned_literals_.size() <= 1u)
            return;

        resolution_chain_literals_.reserve(learned_literals_.size() - 1u);
        for (std::size_t index = 1u; index < learned_literals_.size(); ++index)
            resolution_chain_literals_.push_back(learned_literals_[index]);
    }

    void conflict_analyzer::set_decision_level(const variable var, const std::uint32_t level) noexcept
    {
        const auto it = std::find_if(decision_levels_.begin(), decision_levels_.end(),
                                     [var](const auto& pair) noexcept { return pair.first.index() == var.index(); });

        if (it != decision_levels_.end())
        {
            it->second = level;
            return;
        }

        decision_levels_.push_back(std::pair<variable, std::uint32_t> {var, level});
    }

    std::uint32_t conflict_analyzer::level_of_variable(const variable var) const noexcept
    {
        const auto it = std::find_if(decision_levels_.begin(), decision_levels_.end(),
                                     [var](const auto& pair) noexcept { return pair.first.index() == var.index(); });
        if (it == decision_levels_.end())
            return 0u;
        return it->second;
    }

    std::uint32_t conflict_analyzer::second_highest_decision_level() const noexcept
    {
        if (learned_literals_.size() < 2u)
            return 0u;

        std::uint32_t highest {};
        std::uint32_t second_highest {};
        for (const auto lit: learned_literals_)
        {
            const auto level = level_of_variable(lit.variable_of());
            if (level > highest)
            {
                second_highest = highest;
                highest = level;
            }
            else if ((level < highest) && (level > second_highest))
                second_highest = level;
        }

        return second_highest;
    }
}
