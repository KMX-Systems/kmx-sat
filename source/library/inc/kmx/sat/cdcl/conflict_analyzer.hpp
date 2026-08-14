/// @file inc/kmx/sat/cdcl/conflict_analyzer.hpp
/// @brief Conflict analysis and learned-clause construction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <span>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/cdcl/stack/decision_frame.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/trail.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Conflict analysis and learned-clause construction.
    ///
    /// When `propagator::propagate` detects a falsified clause, `conflict_analyzer::analyze` walks the implication
    /// graph backward from that conflicting clause using `store::assignment::reason_of` and the current decision
    /// level's trail range (from `stack::decision_frame`), resolving reason clauses together until exactly one
    /// literal from the current decision level remains: the classic first Unique Implication Point (1-UIP) scheme
    /// used by GRASP/Chaff-family CDCL solvers. `derive_first_uip` exposes that literal, `compute_backjump_level`
    /// determines how far `engine::backtrack` should unwind (the second-highest decision level among the derived
    /// clause's literals, not necessarily one level back), `collect_bump_candidates` gathers the variables touched
    /// during resolution for `evsids_heap`/`vmtf_queue`/`chb_tracker` activity bumping, and `build_resolution_chain`
    /// records the antecedent sequence `proof::proof_manager` needs to justify the derived clause under LRAT/FRAT.
    /// @note Every meaningful event this class produces (the derived clause, its resolution chain) must be reported
    /// to `proof::proof_manager` before the corresponding learned clause is registered with `clause::learner`.
    class conflict_analyzer final
    {
    public:
        /// @brief Non-owning callback returning a variable's current decision level (0 if unknown/unassigned).
        using level_lookup_t = std::uint32_t (*)(const void* context, variable var) noexcept;

        /// @brief Non-owning callback returning a variable's reason-clause literals, or an empty span if it has none.
        using reason_lookup_t = std::span<const literal> (*)(const void* context, variable var) noexcept;

        /// @brief Constructs a conflict analyzer with no in-progress analysis state.
        /// @throws None (noexcept).
        conflict_analyzer() noexcept = default;

        /// @brief Runs 1-UIP conflict analysis from the current conflicting clause, deriving a new learned clause.
        /// @throws None (noexcept).
        void analyze() noexcept
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
            std::vector<literal> unique_literals;
            unique_literals.reserve(learned_literals_.size());
            for (const auto lit: learned_literals_)
            {
                const auto duplicate_it =
                    std::find_if(unique_literals.begin(), unique_literals.end(), [lit](const literal existing) noexcept
                                 { return existing.variable_of().index() == lit.variable_of().index(); });
                if (duplicate_it == unique_literals.end())
                    unique_literals.push_back(lit);
            }
            learned_literals_ = std::move(unique_literals);

            // Conservative minimization: keep asserting literal and drop level-0 tail literals.
            if (learned_literals_.size() > 1u)
            {
                std::vector<literal> minimized_literals {};
                minimized_literals.reserve(learned_literals_.size());
                minimized_literals.push_back(learned_literals_.front());

                for (std::size_t index = 1u; index < learned_literals_.size(); ++index)
                {
                    const auto lit = learned_literals_[index];
                    if (level_of_variable(lit.variable_of()) == 0u)
                        continue;
                    minimized_literals.push_back(lit);
                }

                learned_literals_ = std::move(minimized_literals);
            }

            // Canonicalize asserting literal position: keep the highest-level literal at index 0.
            if (learned_literals_.size() > 1u)
            {
                std::size_t asserting_index = 0u;
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

        /// @brief Runs real first-UIP conflict analysis by resolving the implication graph backward over reason
        /// clauses, following the classic GRASP/Chaff/MiniSat scheme.
        ///
        /// @details
        /// Starting from the seeded conflict clause (see `seed_conflict_clause`), this walks the assignment trail
        /// backward, resolving on the reason clause of each literal assigned at `current_level` until exactly one
        /// such literal remains unresolved: that literal's negation becomes the derived clause's asserting literal
        /// (index 0). Literals from earlier decision levels are kept in the derived clause; level-0 (root-forced)
        /// literals are dropped since they can never be falsified. The backjump level is the highest decision level
        /// among the kept (non-asserting) literals, or 0 if none remain.
        /// @param trail_in_order Full assignment trail (decisions and implied literals) in chronological order.
        /// @param current_level Decision level at which the conflict was detected.
        /// @param level_of Callback returning a variable's current decision level.
        /// @param reason_of Callback returning a variable's reason-clause literals (empty span if none/decision).
        /// @param context Opaque context forwarded to both callbacks.
        /// @throws None (noexcept).
        void analyze_via_resolution(const std::span<const literal> trail_in_order, const std::uint32_t current_level,
                                    const level_lookup_t level_of, const reason_lookup_t reason_of, const void* context) noexcept
        {
            resolution_chain_literals_.clear();
            resolution_chain_step_count_ = 0u;
            learned_literals_.clear();
            bump_candidates_.clear();
            backjump_level_ = 0u;

            std::vector<literal> conflict_literals {};
            if (pending_conflict_literals_.empty())
                conflict_literals.clear();
            else
            {
                conflict_literals = std::move(pending_conflict_literals_);
                pending_conflict_literals_.clear();
            }

            if (conflict_literals.empty() || level_of == nullptr || reason_of == nullptr)
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
                return index < seen_flags_.size() && seen_flags_[index] != 0u;
            };

            std::vector<literal> tail_literals {};
            std::uint32_t unresolved_at_current_level {};
            bool have_pivot = false;
            literal pivot {};
            std::span<const literal> literals_to_resolve = conflict_literals;
            auto trail_cursor = static_cast<std::ptrdiff_t>(trail_in_order.size()) - 1;

            for (;;)
            {
                for (const auto candidate: literals_to_resolve)
                {
                    const auto candidate_variable = candidate.variable_of();
                    if (have_pivot && candidate_variable.index() == pivot.variable_of().index())
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
                        tail_literals.push_back(candidate);
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
                while (trail_cursor >= 0)
                {
                    const auto trail_literal = trail_in_order[static_cast<std::size_t>(trail_cursor)];
                    --trail_cursor;
                    if (is_seen(trail_literal.variable_of()) && level_of(context, trail_literal.variable_of()) == current_level)
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
            else if (!tail_literals.empty())
            {
                learned_literals_.push_back(tail_literals.front());
                tail_literals.erase(tail_literals.begin());
            }
            else
            {
                return;
            }

            std::uint32_t highest_tail_level {};
            for (const auto lit: tail_literals)
            {
                const auto level = level_of(context, lit.variable_of());
                if (level > highest_tail_level)
                    highest_tail_level = level;
            }
            backjump_level_ = highest_tail_level;

            std::sort(tail_literals.begin(), tail_literals.end(),
                      [level_of, context](const literal lhs, const literal rhs) noexcept
                      {
                          const auto lhs_level = level_of(context, lhs.variable_of());
                          const auto rhs_level = level_of(context, rhs.variable_of());
                          if (lhs_level != rhs_level)
                              return lhs_level > rhs_level;
                          return lhs.raw() < rhs.raw();
                      });

            learned_literals_.insert(learned_literals_.end(), tail_literals.begin(), tail_literals.end());

            std::vector<literal> canonical_literals {};
            canonical_literals.reserve(learned_literals_.size());
            for (const auto lit: learned_literals_)
            {
                const auto duplicate_it =
                    std::find_if(canonical_literals.begin(), canonical_literals.end(), [lit](const literal existing) noexcept
                                 { return existing.variable_of().index() == lit.variable_of().index(); });
                if (duplicate_it == canonical_literals.end())
                    canonical_literals.push_back(lit);
            }
            learned_literals_ = std::move(canonical_literals);
        }

        /// @brief Returns the first Unique Implication Point literal found by the last `analyze` call.
        /// @return Asserting literal of the derived clause.
        /// @throws None (noexcept).
        literal derive_first_uip() const noexcept
        {
            if (learned_literals_.empty())
                return {};
            return learned_literals_.front();
        }

        /// @brief Computes the decision level `engine::backtrack` should unwind to for the derived clause.
        /// @return Backjump target decision level.
        /// @throws None (noexcept).
        std::uint32_t compute_backjump_level() const noexcept { return backjump_level_; }

        /// @brief Returns the variables touched during resolution, to be bumped in the active branching heuristics.
        /// @return Read-only span over bump-candidate variables.
        /// @throws None (noexcept).
        std::span<const variable> collect_bump_candidates() const noexcept { return bump_candidates_; }

        /// @brief Returns the learned clause produced by the most recent `analyze` call.
        /// @return Read-only span over learned literals.
        /// @throws None (noexcept).
        std::span<const literal> learned_clause() const noexcept { return learned_literals_; }

        /// @brief Returns whether the most recent analysis run produced a learned clause.
        /// @return True if `analyze` has produced at least one learned literal.
        [[nodiscard]] bool has_learned_clause() const noexcept { return !learned_literals_.empty(); }

        /// @brief Records the antecedent resolution chain needed to justify the derived clause under LRAT/FRAT.
        /// @throws None (noexcept).
        void build_resolution_chain() noexcept
        {
            resolution_chain_step_count_ = learned_literals_.empty() ? 0 : static_cast<std::uint32_t>(learned_literals_.size() - 1);
            resolution_chain_literals_.clear();
            if (learned_literals_.size() <= 1u)
                return;

            resolution_chain_literals_.reserve(learned_literals_.size() - 1u);
            for (std::size_t index = 1u; index < learned_literals_.size(); ++index)
                resolution_chain_literals_.push_back(learned_literals_[index]);
        }

        /// @brief Returns the literal sequence used to build the current resolution chain.
        /// @return Read-only span over chain literals used as antecedent hints.
        std::span<const literal> resolution_chain_literals() const noexcept { return resolution_chain_literals_; }

        /// @brief Seeds the conflict clause that the next `analyze` call should process.
        /// @param literals Literals from the conflicting clause in analysis order.
        /// @throws None (noexcept).
        void seed_conflict_clause(const std::span<const literal> literals) noexcept
        {
            decision_levels_.clear();
            pending_conflict_literals_.assign(literals.begin(), literals.end());
        }

        /// @brief Records a variable's current decision level for subsequent backjump computation.
        /// @param var Variable whose level is being recorded.
        /// @param level Current assignment decision level.
        /// @throws None (noexcept).
        void set_decision_level(const variable var, const std::uint32_t level) noexcept
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

        /// @brief Returns the number of resolution steps generated by `build_resolution_chain`.
        /// @return Number of recorded resolution steps.
        /// @throws None (noexcept).
        std::uint32_t resolution_chain_step_count() const noexcept { return resolution_chain_step_count_; }

        /// @brief Returns how many variables were collected as bump candidates by the most recent analysis run.
        /// @return Number of candidate variables for heuristic activity bumps.
        std::size_t bump_candidate_count() const noexcept { return bump_candidates_.size(); }

    private:
        std::uint32_t level_of_variable(const variable var) const noexcept
        {
            const auto it = std::find_if(decision_levels_.begin(), decision_levels_.end(),
                                         [var](const auto& pair) noexcept { return pair.first.index() == var.index(); });
            if (it == decision_levels_.end())
                return 0;
            return it->second;
        }

        std::uint32_t second_highest_decision_level() const noexcept
        {
            if (learned_literals_.size() < 2)
                return 0;

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
                else if (level < highest && level > second_highest)
                    second_highest = level;
            }

            return second_highest;
        }

        std::vector<literal> pending_conflict_literals_ {};
        std::vector<literal> learned_literals_ {};
        std::vector<literal> resolution_chain_literals_ {};
        std::vector<variable> bump_candidates_ {};
        std::vector<std::pair<variable, std::uint32_t>> decision_levels_ {};
        std::vector<std::uint8_t> seen_flags_ {};
        std::vector<std::size_t> touched_variable_indices_ {};
        std::uint32_t backjump_level_ {};
        std::uint32_t resolution_chain_step_count_ {};
    };
}
