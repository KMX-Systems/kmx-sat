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
    /// @brief Variables paired with the decision level they were assigned at.
    using variable_level_list_t = std::vector<std::pair<variable, std::uint32_t>>;

    /// @brief Conflict analysis and learned-clause construction.
    /// @details
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
        void analyze() noexcept;

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
                                    const level_lookup_t level_of, const reason_lookup_t reason_of, const void* context) noexcept;

        /// @brief Returns the first Unique Implication Point literal found by the last `analyze` call.
        /// @return Asserting literal of the derived clause.
        /// @throws None (noexcept).
        literal derive_first_uip() const noexcept;

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
        void build_resolution_chain() noexcept;

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
        void set_decision_level(const variable var, const std::uint32_t level) noexcept;

        /// @brief Returns the number of resolution steps generated by `build_resolution_chain`.
        /// @return Number of recorded resolution steps.
        /// @throws None (noexcept).
        std::uint32_t resolution_chain_step_count() const noexcept { return resolution_chain_step_count_; }

        /// @brief Returns how many variables were collected as bump candidates by the most recent analysis run.
        /// @return Number of candidate variables for heuristic activity bumps.
        std::size_t bump_candidate_count() const noexcept { return bump_candidates_.size(); }

    private:
        std::uint32_t level_of_variable(const variable var) const noexcept;

        std::uint32_t second_highest_decision_level() const noexcept;

        std::vector<literal> pending_conflict_literals_ {};
        std::vector<literal> learned_literals_ {};
        std::vector<literal> resolution_chain_literals_ {};
        std::vector<variable> bump_candidates_ {};
        variable_level_list_t decision_levels_ {};
        std::vector<std::uint8_t> seen_flags_ {};
        std::vector<std::size_t> touched_variable_indices_ {};
        std::vector<literal> unique_scratch_ {};
        std::vector<literal> minimized_scratch_ {};
        std::vector<literal> conflict_scratch_ {};
        std::vector<literal> tail_scratch_ {};
        std::vector<literal> canonical_scratch_ {};
        std::uint32_t backjump_level_ {};
        std::uint32_t resolution_chain_step_count_ {};
    };
}
