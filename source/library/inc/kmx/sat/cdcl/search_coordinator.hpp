/// @file inc/kmx/sat/cdcl/search_coordinator.hpp
/// @brief Complete control flow of one CDCL episode.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/engine/backtrack.hpp>
#include <kmx/sat/cdcl/clause/learner.hpp>
#include <kmx/sat/cdcl/conflict_analyzer.hpp>
#include <kmx/sat/cdcl/engine/decision.hpp>
#include <kmx/sat/cdcl/propagator.hpp>
#include <kmx/sat/cdcl/controller/reduce.hpp>
#include <kmx/sat/cdcl/controller/restart.hpp>
#include <kmx/sat/solve_request.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Complete control flow of one CDCL episode.
    ///
    /// @details
    /// `search_coordinator` implements the classic CDCL main loop (`run_search_epoch`) by orchestrating the concrete,
    /// non-virtual components it owns by direct reference/composition (`propagator`, `conflict_analyzer`,
    /// `clause::learner`, `engine::backtrack`, `engine::decision`, `controller::restart`, `controller::reduce`) per
    /// the architecture's composition strategy: propagate to fixpoint or conflict, and on conflict run
    /// `handle_conflict` (analyze, learn, backjump via `engine::backtrack`, re-propagate); on no conflict, check
    /// `controller::restart::should_restart` (`handle_restart`) or ask `engine::decision` for the next branch; detect
    /// termination (`handle_sat`/`handle_unsat`/`handle_termination` for the empty-clause, all-variables-assigned, and
    /// limit/callback-triggered cases respectively). `apply_assumptions` seeds the episode from the active
    /// `solve_request`, propagating assumption-forced literals before the main loop begins.
    /// @note This class plus `solver_core` are held to the same call/memory-layout overhead bar as an equivalent
    /// flat-struct baseline (measured in Phase 5 comparative benchmarking), so the extra decomposition versus
    /// CaDiCaL's monolithic `Internal` does not cost hot-path performance.
    class search_coordinator final
    {
    public:
        /// @brief Terminal outcome produced by the current or most recent search epoch.
        enum class outcome
        {
            /// @brief Search is still in progress.
            in_progress,
            /// @brief Search completed with a satisfiable assignment.
            satisfiable,
            /// @brief Search completed with a proof of unsatisfiability.
            unsatisfiable,
            /// @brief Search terminated because of limits or external cancellation.
            terminated
        };

        /// @brief Constructs a search coordinator with freshly constructed CDCL components.
        /// @throws None (noexcept).
        search_coordinator() noexcept = default;

        /// @brief Runs the CDCL main loop for one solve episode until a terminal outcome is reached.
        /// @throws None (noexcept).
        void run_search_epoch() noexcept
        {
            if (outcome_ != outcome::in_progress)
            {
                return;
            }

            const auto assumption_conflict = propagator_.propagate_assumptions();
            if (assumption_conflict.valid())
            {
                handle_unsat();
                return;
            }

            for (;;)
            {
                const auto conflict = propagator_.propagate();
                if (conflict.valid())
                {
                    handle_conflict();
                    if (outcome_ != outcome::in_progress)
                    {
                        return;
                    }
                    continue;
                }

                if (restart_controller_.should_restart())
                {
                    handle_restart();
                    continue;
                }

                if (reduce_controller_.should_reduce())
                {
                    reduce_controller_.select_reduction_candidates();
                    reduce_controller_.reduce_clauses();
                    reduce_controller_.flush_redundant();
                    reduce_controller_.update_tiers();
                }

                const auto branch_literal = decision_engine_.pick_branch_literal();
                if (!branch_literal.has_value())
                {
                    handle_sat();
                    return;
                }

                restart_controller_.tick_decision();
                ++decision_count_;
                if (decision_limit_ != 0 && decision_count_ >= decision_limit_)
                {
                    handle_termination();
                }
                return;
            }
        }

        /// @brief Seeds the episode's assumptions from the active solve request before the main loop begins.
        /// @param request Solve request describing this episode's assumptions, limits, and mode flags.
        /// @throws None (noexcept).
        void apply_assumptions(const solve_request& request) noexcept
        {
            assumptions_.assign(request.assumptions.begin(), request.assumptions.end());
            propagator_.set_pending_assumption_count(assumptions_.size());
            conflict_limit_ = request.conflict_limit;
            decision_limit_ = request.decision_limit;
            conflict_count_ = 0;
            decision_count_ = 0;
            outcome_ = outcome::in_progress;
        }

        /// @brief Attaches a clause reference to the propagator-owned watch state.
        /// @param ref Clause reference to attach for propagation bookkeeping.
        /// @throws None (noexcept).
        void attach_clause(const clause::ref_t ref) noexcept
        {
            propagator_.attach_clause(ref);
        }

        /// @brief Handles a detected conflict: analyze, learn, backjump, and resume propagation.
        /// @throws None (noexcept).
        void handle_conflict() noexcept
        {
            conflict_analyzer_.analyze();
            conflict_analyzer_.build_resolution_chain();

            const auto learned_clause = conflict_analyzer_.learned_clause();
            if (!learned_clause.empty())
            {
                clause_learner_.learn_clause(learned_clause);
            }

            const auto backjump_level = conflict_analyzer_.compute_backjump_level();
            backtrack_engine_.backtrack_to_level(backjump_level);
            backtrack_engine_.clear_transient_marks();

            const auto asserting_literal = conflict_analyzer_.derive_first_uip();
            if (asserting_literal.raw() != 0)
            {
                clause_learner_.assign_asserting_literal(asserting_literal);
            }

            restart_controller_.tick_conflict();
            ++conflict_count_;
            if (conflict_limit_ != 0 && conflict_count_ >= conflict_limit_)
            {
                handle_termination();
            }
        }

        /// @brief Handles a triggered restart: unwind to decision level zero and resume branching.
        /// @throws None (noexcept).
        void handle_restart() noexcept
        {
            backtrack_engine_.backtrack_to_level(0);
            decision_engine_.notify_restart();
            restart_controller_.reset_after_inprocess();
        }

        /// @brief Handles the satisfiable terminal case (every variable consistently assigned).
        /// @throws None (noexcept).
        void handle_sat() noexcept
        {
            outcome_ = outcome::satisfiable;
        }

        /// @brief Handles the unsatisfiable terminal case (empty clause derived, or assumption-level conflict).
        /// @throws None (noexcept).
        void handle_unsat() noexcept
        {
            outcome_ = outcome::unsatisfiable;
        }

        /// @brief Handles limit exhaustion or an external termination callback firing.
        /// @throws None (noexcept).
        void handle_termination() noexcept
        {
            outcome_ = outcome::terminated;
        }

        /// @brief Returns the terminal state tracked by this coordinator.
        /// @return Current search-epoch outcome.
        /// @throws None (noexcept).
        outcome current_outcome() const noexcept
        {
            return outcome_;
        }

        /// @brief Stages a conflict that will be consumed by the next propagation step.
        /// @param ref Clause reference to report as conflicting.
        /// @throws None (noexcept).
        void stage_conflict(const clause::ref_t ref) noexcept
        {
            propagator_.stage_conflict(ref);
        }

        /// @brief Seeds conflict literals for the next analysis pass.
        /// @param literals Conflicting clause literals in analysis order.
        /// @throws None (noexcept).
        void seed_conflict_clause(const std::span<const literal> literals) noexcept
        {
            conflict_analyzer_.seed_conflict_clause(literals);
        }

        /// @brief Records decision levels used by the analyzer for backjump computation.
        /// @param var Variable whose level is being recorded.
        /// @param level Decision level to associate with the variable.
        /// @throws None (noexcept).
        void set_decision_level(const variable var, const std::uint32_t level) noexcept
        {
            conflict_analyzer_.set_decision_level(var, level);
        }

        /// @brief Seeds the fallback branch variable used by the decision engine.
        /// @param var Variable index to return on the next branch pick.
        /// @throws None (noexcept).
        void set_next_decision_variable(const std::uint32_t var) noexcept
        {
            decision_engine_.set_next_variable(var);
        }

        /// @brief Returns how many learned clauses were registered during this episode.
        /// @return Number of learned clauses in clause_learner.
        /// @throws None (noexcept).
        std::size_t learned_clause_count() const noexcept
        {
            return clause_learner_.learned_clause_count();
        }

        /// @brief Returns the most recently learned clause produced by the clause learner.
        /// @return Read-only span over the latest learned clause.
        /// @throws None (noexcept).
        std::span<const literal> last_learned_clause() const noexcept
        {
            return clause_learner_.last_learned_clause();
        }

        /// @brief Returns the most recently assigned asserting literal.
        /// @return Last asserting literal stored by clause_learner.
        /// @throws None (noexcept).
        literal last_asserting_literal() const noexcept
        {
            return clause_learner_.last_asserting_literal();
        }

        /// @brief Returns the next branching literal selected by the decision engine.
        /// @param fallback_variable Variable index staged into the current lightweight heuristic path.
        /// @return Selected branch literal, or `std::nullopt` if no candidate remains.
        /// @throws None (noexcept).
        std::optional<literal> next_branch_literal(const std::uint32_t fallback_variable) noexcept
        {
            decision_engine_.set_next_variable(fallback_variable);
            return decision_engine_.pick_branch_literal();
        }

        /// @brief Returns the number of assumption-propagation calls executed so far.
        /// @return Number of assumption propagation calls.
        /// @throws None (noexcept).
        std::size_t assumption_propagation_call_count() const noexcept
        {
            return propagator_.assumption_propagation_call_count();
        }

        /// @brief Returns the number of regular propagation calls executed so far.
        /// @return Number of regular propagation calls.
        /// @throws None (noexcept).
        std::size_t propagation_call_count() const noexcept
        {
            return propagator_.propagation_call_count();
        }

        /// @brief Forces the next search loop iteration to execute the restart path.
        /// @throws None (noexcept).
        void request_restart() noexcept
        {
            restart_controller_.request_restart();
        }

        /// @brief Configures periodic restart scheduling by conflict interval.
        /// @param interval Conflicts between restart opportunities (zero disables schedule).
        /// @throws None (noexcept).
        void set_restart_interval(const std::uint64_t interval) noexcept
        {
            restart_controller_.set_restart_interval(interval);
        }

        /// @brief Forces the next search loop iteration to execute one reduction pass.
        /// @throws None (noexcept).
        void request_reduce() noexcept
        {
            reduce_controller_.request_reduce();
        }

        /// @brief Returns how many restart operations were performed.
        /// @return Number of completed restart operations.
        /// @throws None (noexcept).
        std::uint64_t restart_count() const noexcept
        {
            return restart_controller_.restart_count();
        }

        /// @brief Returns the remaining scheduled conflict budget before the next restart opportunity.
        /// @return Remaining restart budget in conflicts.
        /// @throws None (noexcept).
        std::uint64_t current_restart_budget() const noexcept
        {
            return restart_controller_.current_restart_budget();
        }

        /// @brief Returns how many reduction passes were completed.
        /// @return Number of completed reduction passes.
        /// @throws None (noexcept).
        std::uint64_t reduction_pass_count() const noexcept
        {
            return reduce_controller_.reduction_pass_count();
        }

        /// @brief Returns how many conflicts were handled in the current episode.
        /// @return Episode conflict count.
        /// @throws None (noexcept).
        std::uint64_t conflict_event_count() const noexcept
        {
            return conflict_count_;
        }

        /// @brief Returns how many decisions were produced in the current episode.
        /// @return Episode decision count.
        /// @throws None (noexcept).
        std::uint64_t decision_event_count() const noexcept
        {
            return decision_count_;
        }

    private:
        propagator propagator_ {};
        conflict_analyzer conflict_analyzer_ {};
        clause::learner clause_learner_ {};
        engine::backtrack backtrack_engine_ {};
        engine::decision decision_engine_ {};
        controller::restart restart_controller_ {};
        controller::reduce reduce_controller_ {};
        std::vector<literal> assumptions_ {};
        std::uint64_t conflict_limit_ {0};
        std::uint64_t decision_limit_ {0};
        std::uint64_t conflict_count_ {0};
        std::uint64_t decision_count_ {0};
        outcome outcome_ {outcome::in_progress};
    };
}
