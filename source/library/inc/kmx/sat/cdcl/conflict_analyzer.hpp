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
            {
                learned_literals_.clear();
            }
            else
            {
                learned_literals_ = std::move(pending_conflict_literals_);
                pending_conflict_literals_.clear();
            }

            // Keep first-occurrence order while removing duplicates by raw literal encoding.
            std::vector<literal> unique_literals;
            unique_literals.reserve(learned_literals_.size());
            for (const auto lit: learned_literals_)
            {
                const auto duplicate_it = std::find_if(unique_literals.begin(), unique_literals.end(),
                                                       [lit](const literal existing) noexcept { return existing.raw() == lit.raw(); });
                if (duplicate_it == unique_literals.end())
                {
                    unique_literals.push_back(lit);
                }
            }
            learned_literals_ = std::move(unique_literals);

            bump_candidates_.clear();
            bump_candidates_.reserve(learned_literals_.size());
            for (const auto lit: learned_literals_)
            {
                const auto var = lit.variable_of();
                const auto seen_it = std::find_if(bump_candidates_.begin(), bump_candidates_.end(),
                                                  [var](const variable existing) noexcept { return existing.index() == var.index(); });
                if (seen_it == bump_candidates_.end())
                {
                    bump_candidates_.push_back(var);
                }
            }

            backjump_level_ = second_highest_decision_level();
        }

        /// @brief Returns the first Unique Implication Point literal found by the last `analyze` call.
        /// @return Asserting literal of the derived clause.
        /// @throws None (noexcept).
        literal derive_first_uip() const noexcept
        {
            if (learned_literals_.empty())
            {
                return {};
            }
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
            {
                return;
            }

            resolution_chain_literals_.reserve(learned_literals_.size() - 1u);
            for (std::size_t index = 1u; index < learned_literals_.size(); ++index)
            {
                resolution_chain_literals_.push_back(learned_literals_[index]);
            }
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
            {
                return 0;
            }
            return it->second;
        }

        std::uint32_t second_highest_decision_level() const noexcept
        {
            if (learned_literals_.size() < 2)
            {
                return 0;
            }

            std::uint32_t highest {0};
            std::uint32_t second_highest {0};
            for (const auto lit: learned_literals_)
            {
                const auto level = level_of_variable(lit.variable_of());
                if (level >= highest)
                {
                    second_highest = highest;
                    highest = level;
                }
                else if (level > second_highest)
                {
                    second_highest = level;
                }
            }

            return second_highest;
        }

        std::vector<literal> pending_conflict_literals_ {};
        std::vector<literal> learned_literals_ {};
        std::vector<literal> resolution_chain_literals_ {};
        std::vector<variable> bump_candidates_ {};
        std::vector<std::pair<variable, std::uint32_t>> decision_levels_ {};
        std::uint32_t backjump_level_ {0};
        std::uint32_t resolution_chain_step_count_ {0};
    };
}
