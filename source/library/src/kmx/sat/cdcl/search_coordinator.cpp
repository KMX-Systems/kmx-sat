/// @file library/src/kmx/sat/cdcl/search_coordinator.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/search_coordinator.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/search_coordinator.hpp>

namespace kmx::sat::cdcl
{
    void search_coordinator::run_search_epoch() noexcept
    {
        if (outcome_ != outcome::in_progress)
            return;

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
                    return;
                continue;
            }

            if (restart_controller_.should_restart())
            {
                handle_restart();
                continue;
            }

            if (reduce_controller_.should_reduce())
            {
                if (clause_database_ != nullptr)
                {
                    reduce_controller_.select_reduction_candidates(*clause_database_);
                    reduce_controller_.reduce_clauses(*clause_database_);
                    reduce_controller_.flush_redundant(*clause_database_);
                }
                else
                {
                    reduce_controller_.select_reduction_candidates();
                    reduce_controller_.reduce_clauses();
                    reduce_controller_.flush_redundant();
                }
                if (clause_database_ != nullptr)
                    reduce_controller_.update_tiers(*clause_database_);
                else
                    reduce_controller_.update_tiers();
                if (clause_database_ != nullptr)
                    clause_database_->decay_quality();
            }

            const auto branch_literal = decision_engine_.pick_branch_literal();
            if (!branch_literal.has_value())
            {
                handle_sat();
                return;
            }

            restart_controller_.tick_decision();
            ++decision_count_;
            if ((decision_limit_ != 0u) && (decision_count_ >= decision_limit_))
            {
                handle_termination(termination_cause::decision_limit);
                return;
            }
            if (restart_controller_.should_restart())
                handle_restart();
            return;
        }
    }

    void search_coordinator::apply_assumptions(const solve_request& request) noexcept
    {
        assumptions_.assign(request.assumptions.begin(), request.assumptions.end());
        propagator_.reset_episode_state();
        propagator_.set_pending_assumption_count(assumptions_.size());
        restart_controller_.reset();
        reduce_controller_.reset();
        conflict_limit_ = request.conflict_limit;
        decision_limit_ = request.decision_limit;
        conflict_count_ = 0u;
        decision_count_ = 0u;
        lean_learned_clause_count_ = 0u;
        inprocess_epoch_notifications_ = 0u;
        low_yield_inprocess_epoch_count_ = 0u;
        low_yield_inprocess_streak_ = 0u;
        deferred_inprocess_resync_count_ = 0u;
        deferred_inprocess_streak_ = 0u;
        termination_cause_ = termination_cause::none;
        outcome_ = outcome::in_progress;
    }

    void search_coordinator::note_conflict(const std::uint32_t glue) noexcept
    {
        restart_controller_.tick_conflict();
        restart_controller_.observe_glue(glue);
        reduce_controller_.tick_conflict();
        ++conflict_count_;
        if ((conflict_limit_ != 0u) && (conflict_count_ >= conflict_limit_))
            handle_termination(termination_cause::conflict_limit);
    }

    void search_coordinator::note_decision() noexcept
    {
        restart_controller_.tick_decision();
        ++decision_count_;
        if ((decision_limit_ != 0u) && (decision_count_ >= decision_limit_))
            handle_termination(termination_cause::decision_limit);
    }

    void search_coordinator::handle_conflict() noexcept
    {
        conflict_analyzer_.analyze();
        conflict_analyzer_.build_resolution_chain();
        finish_conflict_handling();
    }

    void search_coordinator::handle_conflict_via_resolution(const std::span<const literal> trail_in_order,
                                                            const std::uint32_t current_level,
                                                            const conflict_analyzer::level_lookup_t level_of,
                                                            const conflict_analyzer::reason_lookup_t reason_of,
                                                            const void* context) noexcept
    {
        conflict_analyzer_.analyze_via_resolution(trail_in_order, current_level, level_of, reason_of, context);
        conflict_analyzer_.build_resolution_chain();
        finish_conflict_handling();
    }

    void search_coordinator::handle_restart() noexcept
    {
        backtrack_engine_.backtrack_to_level(0u);
        decision_engine_.notify_restart();
        restart_controller_.reset_after_inprocess();
    }

    std::optional<literal> search_coordinator::next_branch_literal(const std::uint32_t fallback_variable) noexcept
    {
        if (fallback_variable != 0u)
            decision_engine_.set_next_variable(fallback_variable);
        return decision_engine_.pick_branch_literal();
    }

    std::optional<literal> search_coordinator::take_branch_literal(const std::uint32_t fallback_variable) noexcept
    {
        if (outcome_ != outcome::in_progress)
            return {};

        if (restart_controller_.should_restart())
            handle_restart();

        if (reduce_controller_.should_reduce())
        {
            if (clause_database_ != nullptr)
            {
                reduce_controller_.select_reduction_candidates(*clause_database_);
                reduce_controller_.reduce_clauses(*clause_database_);
                reduce_controller_.flush_redundant(*clause_database_);
            }
            else
            {
                reduce_controller_.select_reduction_candidates();
                reduce_controller_.reduce_clauses();
                reduce_controller_.flush_redundant();
            }
            if (clause_database_ != nullptr)
                reduce_controller_.update_tiers(*clause_database_);
            else
                reduce_controller_.update_tiers();
            if (clause_database_ != nullptr)
                clause_database_->decay_quality();
        }

        if (fallback_variable != 0u)
            decision_engine_.set_next_variable(fallback_variable);
        const auto branch_literal = decision_engine_.pick_branch_literal();
        if (!branch_literal.has_value())
        {
            // Only an exhausted candidate source with no caller-supplied fallback means every variable is
            // assigned. When the caller passed a fallback it has already found an unassigned variable, so
            // an empty pick is heuristic exhaustion and must not be recorded as a satisfying assignment.
            if (fallback_variable == 0u)
                handle_sat();
            return {};
        }

        restart_controller_.tick_decision();
        ++decision_count_;
        if ((decision_limit_ != 0u) && (decision_count_ >= decision_limit_))
            handle_termination(termination_cause::decision_limit);

        return branch_literal;
    }

    void search_coordinator::notify_inprocess_epoch_completed(const counter_t structural_gain) noexcept
    {
        ++inprocess_epoch_notifications_;
        if (structural_gain == 0u)
        {
            ++low_yield_inprocess_epoch_count_;
            ++low_yield_inprocess_streak_;
            if (low_yield_inprocess_streak_ >= 2u)
            {
                if (deferred_inprocess_streak_ < max_deferred_inprocess_resync_streak)
                {
                    ++deferred_inprocess_resync_count_;
                    ++deferred_inprocess_streak_;
                    return;
                }

                // Safety guard: force an eventual restart-controller resync so deferrals cannot starve forever.
                deferred_inprocess_streak_ = 0u;
                low_yield_inprocess_streak_ = 0u;
            }
        }
        else
        {
            low_yield_inprocess_streak_ = 0u;
            deferred_inprocess_streak_ = 0u;
        }

        restart_controller_.resynchronize_after_inprocess();
    }

    void search_coordinator::finish_conflict_handling() noexcept
    {
        const auto learned_clause = conflict_analyzer_.learned_clause();
        bool learned_clause_registered {};
        if (!learned_clause.empty())
        {
            const auto learned_ref = clause_learner_.learn_clause(learned_clause);
            if (learned_ref.valid())
            {
                learned_clause_registered = true;
                decision_engine_.notify_learned_clause(clause_learner_.last_learned_clause());
            }
        }

        const auto backjump_level = conflict_analyzer_.compute_backjump_level();
        backtrack_engine_.backtrack_to_level(backjump_level);
        backtrack_engine_.clear_transient_marks();

        const auto asserting_literal = conflict_analyzer_.derive_first_uip();
        if (learned_clause_registered && (asserting_literal.raw() != 0u))
        {
            clause_learner_.assign_asserting_literal(asserting_literal);
            decision_engine_.notify_assignment_literal(asserting_literal);
        }

        decision_engine_.notify_conflict_variables(conflict_analyzer_.collect_bump_candidates());
        decision_engine_.notify_conflict();

        restart_controller_.tick_conflict();
        reduce_controller_.tick_conflict();
        ++conflict_count_;
        if ((conflict_limit_ != 0u) && (conflict_count_ >= conflict_limit_))
            handle_termination(termination_cause::conflict_limit);
    }
}
