/// @file inc/kmx/sat/cdcl/search_coordinator.hpp
/// @brief Complete control flow of one CDCL episode.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/learner.hpp>
#include <kmx/sat/cdcl/conflict_analyzer.hpp>
#include <kmx/sat/cdcl/controller/reduce.hpp>
#include <kmx/sat/cdcl/controller/restart.hpp>
#include <kmx/sat/cdcl/engine/backtrack.hpp>
#include <kmx/sat/cdcl/engine/decision.hpp>
#include <kmx/sat/cdcl/propagator.hpp>
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
        using variable_selectable_predicate_t = engine::decision::selectable_predicate_t;

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

        /// @brief Why the current episode terminated, if it terminated.
        enum class termination_cause
        {
            /// @brief The episode did not terminate via handle_termination.
            none,
            /// @brief Termination was triggered by the conflict limit.
            conflict_limit,
            /// @brief Termination was triggered by the decision limit.
            decision_limit,
            /// @brief Termination was triggered by a non-limit source.
            external
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
                    reduce_controller_.update_tiers();
                    if (clause_database_ != nullptr)
                    {
                        clause_database_->decay_quality();
                    }
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
                    handle_termination(termination_cause::decision_limit);
                    return;
                }
                if (restart_controller_.should_restart())
                {
                    handle_restart();
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
            propagator_.reset_episode_state();
            propagator_.set_pending_assumption_count(assumptions_.size());
            restart_controller_.reset();
            reduce_controller_.reset();
            conflict_limit_ = request.conflict_limit;
            decision_limit_ = request.decision_limit;
            conflict_count_ = 0;
            decision_count_ = 0;
            inprocess_epoch_notifications_ = 0u;
            low_yield_inprocess_epoch_count_ = 0u;
            low_yield_inprocess_streak_ = 0u;
            deferred_inprocess_resync_count_ = 0u;
            deferred_inprocess_streak_ = 0u;
            termination_cause_ = termination_cause::none;
            outcome_ = outcome::in_progress;
        }

        /// @brief Attaches a clause reference to the propagator-owned watch state.
        /// @param ref Clause reference to attach for propagation bookkeeping.
        /// @throws None (noexcept).
        void attach_clause(const clause::ref_t ref) noexcept { propagator_.attach_clause(ref); }

        /// @brief Attaches the clause database used by reduction passes.
        /// @param database Clause database to use for reduction candidate selection and flushing.
        /// @throws None (noexcept).
        void attach_database(clause::database& database) noexcept { clause_database_ = &database; }

        /// @brief Handles a detected conflict: analyze, learn, backjump, and resume propagation.
        /// @throws None (noexcept).
        void handle_conflict() noexcept
        {
            conflict_analyzer_.analyze();
            conflict_analyzer_.build_resolution_chain();
            finish_conflict_handling();
        }

        /// @brief Handles a detected conflict using real implication-graph first-UIP resolution.
        /// @param trail_in_order Full assignment trail (decisions and implied literals) in chronological order.
        /// @param current_level Decision level at which the conflict was detected.
        /// @param level_of Callback returning a variable's current decision level.
        /// @param reason_of Callback returning a variable's reason-clause literals.
        /// @param context Opaque context forwarded to both callbacks.
        /// @throws None (noexcept).
        void handle_conflict_via_resolution(const std::span<const literal> trail_in_order, const std::uint32_t current_level,
                                            const conflict_analyzer::level_lookup_t level_of,
                                            const conflict_analyzer::reason_lookup_t reason_of, const void* context) noexcept
        {
            conflict_analyzer_.analyze_via_resolution(trail_in_order, current_level, level_of, reason_of, context);
            conflict_analyzer_.build_resolution_chain();
            finish_conflict_handling();
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
        void handle_sat() noexcept { termination_cause_ = termination_cause::none; outcome_ = outcome::satisfiable; }

        /// @brief Handles the unsatisfiable terminal case (empty clause derived, or assumption-level conflict).
        /// @throws None (noexcept).
        void handle_unsat() noexcept { termination_cause_ = termination_cause::none; outcome_ = outcome::unsatisfiable; }

        /// @brief Handles limit exhaustion or an external termination callback firing.
        /// @throws None (noexcept).
        void handle_termination(const termination_cause cause = termination_cause::external) noexcept { termination_cause_ = cause; outcome_ = outcome::terminated; }

        /// @brief Returns the terminal state tracked by this coordinator.
        /// @return Current search-epoch outcome.
        /// @throws None (noexcept).
        outcome current_outcome() const noexcept { return outcome_; }

        /// @brief Returns the cause of termination for the current episode.
        /// @return Current termination cause, or none if not terminated by limits or callback.
        termination_cause current_termination_cause() const noexcept { return termination_cause_; }

        /// @brief Stages a conflict that will be consumed by the next propagation step.
        /// @param ref Clause reference to report as conflicting.
        /// @throws None (noexcept).
        void stage_conflict(const clause::ref_t ref) noexcept { propagator_.stage_conflict(ref); }

        /// @brief Seeds conflict literals for the next analysis pass.
        /// @param literals Conflicting clause literals in analysis order.
        /// @throws None (noexcept).
        void seed_conflict_clause(const std::span<const literal> literals) noexcept { conflict_analyzer_.seed_conflict_clause(literals); }

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
        void set_next_decision_variable(const std::uint32_t var) noexcept { decision_engine_.set_next_variable(var); }

        /// @brief Configures an optional branch-candidate selectability filter used by decision heuristics.
        /// @param predicate Callback returning true when a candidate variable should be considered selectable.
        /// @param context Opaque callback context (may be null).
        void set_variable_selectability_filter(const variable_selectable_predicate_t predicate, const void* context) noexcept
        {
            decision_engine_.set_selectability_filter(predicate, context);
        }

        /// @brief Registers a formula variable with the active branch heuristics.
        /// @param var Variable to make available for selection.
        /// @throws None (noexcept).
        void activate_variable(const variable var) noexcept { decision_engine_.activate_variable(var); }

        /// @brief Clears any configured branch-candidate selectability filter.
        void clear_variable_selectability_filter() noexcept { decision_engine_.clear_selectability_filter(); }

        /// @brief Configures decision-heuristic maintenance intervals.
        /// @param conflict_maintenance_interval EVSIDS rescale interval in conflicts (0 disables).
        /// @param chb_decay_interval CHB decay interval in conflicts (0 disables).
        /// @param restart_decay_interval CHB decay interval in restarts (0 disables).
        void set_decision_maintenance_intervals(const std::uint32_t conflict_maintenance_interval,
                                                const std::uint32_t chb_decay_interval,
                                                const std::uint32_t restart_decay_interval) noexcept
        {
            decision_engine_.set_maintenance_intervals(conflict_maintenance_interval, chb_decay_interval, restart_decay_interval);
        }

        /// @brief Enables or disables the research-track CHB candidate source.
        void set_chb_enabled(const bool enabled) noexcept { decision_engine_.set_chb_enabled(enabled); }

        /// @brief Returns whether the CHB candidate source is enabled.
        bool chb_enabled() const noexcept { return decision_engine_.chb_enabled(); }

        /// @brief Returns how many EVSIDS rescale maintenance steps were executed.
        std::uint32_t decision_evsids_rescale_count() const noexcept { return decision_engine_.evsids_rescale_count(); }

        /// @brief Returns how many CHB decay maintenance steps were executed.
        std::uint32_t decision_chb_decay_count() const noexcept { return decision_engine_.chb_decay_count(); }

        /// @brief Returns configured decision maintenance intervals in conflict/conflict/restart order.
        std::array<std::uint32_t, 3> decision_maintenance_intervals() const noexcept
        {
            return {decision_engine_.conflict_maintenance_interval(), decision_engine_.chb_decay_interval(),
                    decision_engine_.restart_decay_interval()};
        }

        /// @brief Feeds unit-propagation implied variables into the decision-engine heuristic state.
        /// @param variables Variables implied during recent propagation steps.
        /// @throws None (noexcept).
        void notify_propagated_variables(const std::span<const variable> variables) noexcept
        {
            decision_engine_.notify_propagated_variables(variables);
        }

        /// @brief Feeds newly learned-clause literals into the decision-engine heuristic state.
        /// @param learned_clause Literals of the learned clause.
        /// @throws None (noexcept).
        void notify_learned_clause(const std::span<const literal> learned_clause) noexcept
        {
            decision_engine_.notify_learned_clause(learned_clause);
        }

        /// @brief Records an executed assignment literal in decision heuristics for saved-phase reuse.
        /// @param assigned_literal Literal assigned by assumptions/propagation/branching.
        /// @throws None (noexcept).
        void notify_assignment_literal(const literal assigned_literal) noexcept
        {
            decision_engine_.notify_assignment_literal(assigned_literal);
        }

        /// @brief Feeds raw conflict-clause literals into decision heuristics for polarity and activity updates.
        /// @param conflict_clause Literals from the conflicting clause.
        /// @throws None (noexcept).
        void notify_conflict_clause(const std::span<const literal> conflict_clause) noexcept
        {
            decision_engine_.notify_conflict_clause(conflict_clause);
        }

        /// @brief Returns how many learned clauses were registered during this episode.
        /// @return Number of learned clauses in clause_learner.
        /// @throws None (noexcept).
        std::size_t learned_clause_count() const noexcept { return clause_learner_.learned_clause_count(); }

        /// @brief Returns the most recently learned clause produced by the clause learner.
        /// @return Read-only span over the latest learned clause.
        /// @throws None (noexcept).
        std::span<const literal> last_learned_clause() const noexcept { return clause_learner_.last_learned_clause(); }

        /// @brief Returns the most recent resolution-chain literals produced by conflict analysis.
        /// @return Read-only span over resolution-chain literals.
        std::span<const literal> last_resolution_chain_literals() const noexcept { return conflict_analyzer_.resolution_chain_literals(); }

        /// @brief Returns the most recently assigned asserting literal.
        /// @return Last asserting literal stored by clause_learner.
        /// @throws None (noexcept).
        literal last_asserting_literal() const noexcept { return clause_learner_.last_asserting_literal(); }

        /// @brief Returns the next branching literal selected by the decision engine.
        /// @param fallback_variable Variable index staged into the current lightweight heuristic path.
        /// @return Selected branch literal, or `std::nullopt` if no candidate remains.
        /// @throws None (noexcept).
        std::optional<literal> next_branch_literal(const std::uint32_t fallback_variable) noexcept
        {
            if (fallback_variable != 0u)
            {
                decision_engine_.set_next_variable(fallback_variable);
            }
            return decision_engine_.pick_branch_literal();
        }

        /// @brief Advances restart/reduction/decision bookkeeping and returns the next branch literal.
        /// @param fallback_variable Variable index staged into the current lightweight heuristic path.
        /// @return Selected branch literal, or `std::nullopt` if the episode terminated or no candidate remains.
        /// @throws None (noexcept).
        std::optional<literal> take_branch_literal(const std::uint32_t fallback_variable) noexcept
        {
            if (outcome_ != outcome::in_progress)
            {
                return std::nullopt;
            }

            if (restart_controller_.should_restart())
            {
                handle_restart();
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
                reduce_controller_.update_tiers();
                if (clause_database_ != nullptr)
                {
                    clause_database_->decay_quality();
                }
            }

            if (fallback_variable != 0u)
            {
                decision_engine_.set_next_variable(fallback_variable);
            }
            const auto branch_literal = decision_engine_.pick_branch_literal();
            if (!branch_literal.has_value())
            {
                handle_sat();
                return std::nullopt;
            }

            restart_controller_.tick_decision();
            ++decision_count_;
            if (decision_limit_ != 0 && decision_count_ >= decision_limit_)
            {
                handle_termination(termination_cause::decision_limit);
            }

            return branch_literal;
        }

        /// @brief Returns the number of assumption-propagation calls executed so far.
        /// @return Number of assumption propagation calls.
        /// @throws None (noexcept).
        std::size_t assumption_propagation_call_count() const noexcept { return propagator_.assumption_propagation_call_count(); }

        /// @brief Returns the number of regular propagation calls executed so far.
        /// @return Number of regular propagation calls.
        /// @throws None (noexcept).
        std::size_t propagation_call_count() const noexcept { return propagator_.propagation_call_count(); }

        /// @brief Forces the next search loop iteration to execute the restart path.
        /// @throws None (noexcept).
        void request_restart() noexcept { restart_controller_.request_restart(); }

        /// @brief Configures periodic restart scheduling by conflict interval.
        /// @param interval Conflicts between restart opportunities (zero disables schedule).
        /// @throws None (noexcept).
        void set_restart_interval(const std::uint64_t interval) noexcept { restart_controller_.set_restart_interval(interval); }

        /// @brief Configures periodic restart scheduling by decision interval.
        /// @param interval Decisions between restart opportunities (zero disables schedule).
        /// @throws None (noexcept).
        void set_decision_restart_interval(const std::uint64_t interval) noexcept
        {
            restart_controller_.set_decision_restart_interval(interval);
        }

        /// @brief Records learned-clause glue for the optional adaptive restart policy.
        void notify_learned_glue(const std::uint32_t glue) noexcept { restart_controller_.observe_glue(glue); }

        /// @brief Configures the optional fast/slow glue restart ratio.
        void set_glue_restart_threshold(const double ratio) noexcept { restart_controller_.set_glue_restart_threshold(ratio); }

        double fast_glue_ema() const noexcept { return restart_controller_.fast_glue_ema(); }
        double slow_glue_ema() const noexcept { return restart_controller_.slow_glue_ema(); }
        std::uint32_t glue_restart_threshold_percent() const noexcept
        {
            return restart_controller_.glue_restart_threshold_percent();
        }

        /// @brief Forces the next search loop iteration to execute one reduction pass.
        /// @throws None (noexcept).
        void request_reduce() noexcept { reduce_controller_.request_reduce(); }

        /// @brief Configures the percentage of ranked learned clauses eligible for reduction.
        void set_reduction_fraction_percent(const std::uint32_t percent) noexcept
        {
            reduce_controller_.set_reduction_fraction_percent(percent);
        }

        /// @brief Returns the configured reduction quota percentage.
        std::uint32_t reduction_fraction_percent() const noexcept { return reduce_controller_.reduction_fraction_percent(); }

        /// @brief Configures activity protection for low-glue learned clauses.
        void set_activity_retention_threshold(const double threshold) noexcept
        {
            reduce_controller_.set_activity_retention_threshold(threshold);
        }

        /// @brief Returns the activity threshold protecting low-glue learned clauses.
        double activity_retention_threshold() const noexcept { return reduce_controller_.activity_retention_threshold(); }

        /// @brief Returns how many restart operations were performed.
        /// @return Number of completed restart operations.
        /// @throws None (noexcept).
        std::uint64_t restart_count() const noexcept { return restart_controller_.restart_count(); }

        /// @brief Returns the remaining scheduled conflict budget before the next restart opportunity.
        /// @return Remaining restart budget in conflicts.
        /// @throws None (noexcept).
        std::uint64_t current_restart_budget() const noexcept { return restart_controller_.current_restart_budget(); }

        /// @brief Returns the remaining scheduled decision budget before the next restart opportunity.
        /// @return Remaining restart budget in decisions.
        /// @throws None (noexcept).
        std::uint64_t current_decision_restart_budget() const noexcept { return restart_controller_.current_decision_restart_budget(); }

        /// @brief Returns how many reduction passes were completed.
        /// @return Number of completed reduction passes.
        /// @throws None (noexcept).
        std::uint64_t reduction_pass_count() const noexcept { return reduce_controller_.reduction_pass_count(); }

        std::uint64_t reduced_clause_count() const noexcept { return reduce_controller_.reduced_candidates(); }

        std::uint64_t deleted_clause_count() const noexcept { return reduce_controller_.flushed_candidates(); }

        /// @brief Returns how many conflicts were handled in the current episode.
        /// @return Episode conflict count.
        /// @throws None (noexcept).
        std::uint64_t conflict_event_count() const noexcept { return conflict_count_; }

        /// @brief Returns how many decisions were produced in the current episode.
        /// @return Episode decision count.
        /// @throws None (noexcept).
        std::uint64_t decision_event_count() const noexcept { return decision_count_; }

        /// @brief Notifies the coordinator that an inprocessing epoch has completed.
        /// @throws None (noexcept).
        void notify_inprocess_epoch_completed() noexcept { notify_inprocess_epoch_completed(1u); }

        /// @brief Notifies the coordinator that an inprocessing epoch has completed and reports structural yield.
        /// @param structural_gain Clauses removed by the epoch (zero means no structural reduction).
        /// @throws None (noexcept).
        void notify_inprocess_epoch_completed(const std::uint64_t structural_gain) noexcept
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

            restart_controller_.reset_after_inprocess();
        }

        /// @brief Returns how many inprocess epoch completion notifications were observed.
        /// @return Total number of notifications.
        std::uint64_t inprocess_epoch_notification_count() const noexcept { return inprocess_epoch_notifications_; }

        /// @brief Returns how many low-yield inprocess epochs were observed.
        /// @return Number of zero-gain notifications.
        std::uint64_t low_yield_inprocess_epoch_count() const noexcept { return low_yield_inprocess_epoch_count_; }

        /// @brief Returns how many restart resync operations were deferred after repeated low-yield epochs.
        /// @return Number of deferred resyncs.
        std::uint64_t deferred_inprocess_resync_count() const noexcept { return deferred_inprocess_resync_count_; }

    private:
        /// @brief Shared post-analysis conflict handling: learn, backjump, heuristic feedback, and limit checks.
        /// @throws None (noexcept).
        void finish_conflict_handling() noexcept
        {
            const auto learned_clause = conflict_analyzer_.learned_clause();
            bool learned_clause_registered = false;
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
            if (learned_clause_registered && asserting_literal.raw() != 0)
            {
                clause_learner_.assign_asserting_literal(asserting_literal);
                decision_engine_.notify_assignment_literal(asserting_literal);
            }

            decision_engine_.notify_conflict_variables(conflict_analyzer_.collect_bump_candidates());
            decision_engine_.notify_conflict();

            restart_controller_.tick_conflict();
            ++conflict_count_;
            if (conflict_limit_ != 0 && conflict_count_ >= conflict_limit_)
            {
                handle_termination(termination_cause::conflict_limit);
            }
        }

        static constexpr std::uint64_t max_deferred_inprocess_resync_streak {1u};

        propagator propagator_ {};
        conflict_analyzer conflict_analyzer_ {};
        clause::learner clause_learner_ {};
        engine::backtrack backtrack_engine_ {};
        engine::decision decision_engine_ {};
        controller::restart restart_controller_ {};
        controller::reduce reduce_controller_ {};
        clause::database* clause_database_ {};
        std::vector<literal> assumptions_ {};
        std::uint64_t conflict_limit_ {};
        std::uint64_t decision_limit_ {};
        std::uint64_t conflict_count_ {};
        std::uint64_t decision_count_ {};
        std::uint64_t inprocess_epoch_notifications_ {};
        std::uint64_t low_yield_inprocess_epoch_count_ {};
        std::uint64_t low_yield_inprocess_streak_ {};
        std::uint64_t deferred_inprocess_resync_count_ {};
        std::uint64_t deferred_inprocess_streak_ {};
        termination_cause termination_cause_ {termination_cause::none};
        outcome outcome_ {outcome::in_progress};
    };
}
