/// @file inc/kmx/sat/cdcl/controller/restart.hpp
/// @brief Reluctant doubling, conflict intervals, and EMA-based triggers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/counter.hpp>

namespace kmx::sat::cdcl::controller
{
    /// @brief Reluctant doubling, conflict intervals, and EMA-based triggers.
    /// @details
    /// `controller::restart` decides when `search_coordinator::handle_restart` should unwind the trail back to
    /// decision level zero and let `engine::decision` branch afresh, a key CDCL technique for escaping
    /// heavy-tailed runtimes on hard instances. `tick_conflict`/`tick_decision` advance internal counters used by two
    /// complementary trigger families: Luby/reluctant-doubling conflict-count intervals (as in MiniSat/CaDiCaL) and
    /// glue-based EMA triggers (fast-vs-slow glue moving averages, as in Kissat/Glucose-style solvers, sourced from
    /// `telemetry::ema_tracker`). `should_restart` reports whether either trigger has fired; `current_restart_budget`
    /// exposes the remaining conflict budget before the next scheduled restart; `reset_after_inprocess` resynchronizes
    /// counters after `simplify::scheduler::inprocess` changes the clause set, since inprocessing epochs should not
    /// count toward the interval that was accumulating beforehand.
    class restart final
    {
    public:
        /// @brief Constructs a restart controller with default conflict/EMA trigger state.
        /// @throws None (noexcept).
        restart() noexcept = default;

        /// @brief Checks whether a restart should be performed now.
        /// @return True if either the conflict-interval or EMA-based trigger has fired.
        /// @throws None (noexcept).
        bool should_restart() const noexcept { return restart_pending_; }

        /// @brief Returns whether a restart is currently pending for the next coordination step.
        /// @return True if a restart has been requested or a scheduled trigger fired.
        [[nodiscard]] bool has_pending_restart() const noexcept { return restart_pending_; }

        /// @brief Returns whether a conflict has been recorded since the last performed restart.
        /// @details Restarts are only honoured once the search has learned something new. A restart that unwinds
        /// the trail without an intervening conflict removes work and adds none, so a decision-triggered schedule
        /// could otherwise undo the very decision that triggered it and loop forever without reaching a conflict.
        /// Requiring one conflict per restart gives each restart a learned clause, which is what makes progress --
        /// and therefore termination -- an argument rather than a hope.
        /// @throws None (noexcept).
        [[nodiscard]] bool has_progress_since_restart() const noexcept { return conflict_count_ > conflicts_at_last_restart_; }

        /// @brief Resets all restart scheduler counters and pending state for a fresh solve episode.
        /// @throws None (noexcept).
        void reset() noexcept;

        /// @brief Advances restart bookkeeping by one conflict.
        /// @throws None (noexcept).
        void tick_conflict() noexcept;

        /// @brief Records a learned-clause glue value for the optional EMA restart policy.
        void observe_glue(const std::uint32_t glue) noexcept;

        /// @brief Configures the optional fast/slow glue ratio that requests a restart.
        /// @param ratio Ratio above one; zero disables the EMA trigger.
        void set_glue_restart_threshold(const double ratio) noexcept { glue_restart_threshold_ = (ratio > 1.0) ? ratio : 0.0; }

        std::uint32_t glue_restart_threshold_percent() const noexcept
        {
            return static_cast<std::uint32_t>(glue_restart_threshold_ * 100.0);
        }

        double fast_glue_ema() const noexcept { return fast_glue_ema_; }
        double slow_glue_ema() const noexcept { return slow_glue_ema_; }
        counter_t glue_observation_count() const noexcept { return glue_observation_count_; }

        /// @brief Advances restart bookkeeping by one decision.
        /// @throws None (noexcept).
        void tick_decision() noexcept;

        /// @brief Resynchronizes restart counters after an inprocessing epoch changes the clause set.
        /// @throws None (noexcept).
        void reset_after_inprocess() noexcept;

        /// @brief Resynchronizes decision-scheduled restarts after an inprocessing epoch.
        /// @details Deliberately leaves the *conflict* schedule alone. Inprocessing epochs complete roughly every
        /// thirty conflicts, so rebasing `next_scheduled_restart_at_` to `conflict_count_ + restart_interval_` on
        /// each one pushes the trigger further away than the counter can ever reach: any restart interval above the
        /// epoch cadence is starved outright, and only intervals below it ever fire. The conflict budget is what
        /// the restart policy is expressed in, so it is left to accumulate across epochs.
        /// @throws None (noexcept).
        void resynchronize_after_inprocess() noexcept
        {
            if (decision_restart_interval_ != 0u)
                next_scheduled_decision_restart_at_ = decision_count_ + next_decision_restart_budget();
        }

        /// @brief Returns the remaining conflict budget before the next scheduled restart.
        /// @return Remaining conflict budget.
        /// @throws None (noexcept).
        counter_t current_restart_budget() const noexcept;

        /// @brief Forces `should_restart` to fire until the next `reset_after_inprocess`.
        /// @throws None (noexcept).
        void request_restart() noexcept { restart_pending_ = true; }

        /// @brief Sets the periodic conflict interval used for scheduled restart triggers.
        /// @param interval Number of conflicts between scheduled restart opportunities (zero disables schedule).
        /// @throws None (noexcept).
        void set_restart_interval(const counter_t interval) noexcept;

        /// @brief Sets a periodic decision interval used for scheduled restart triggers.
        /// @param interval Number of decisions between restart opportunities (zero disables decision schedule).
        /// @throws None (noexcept).
        void set_decision_restart_interval(const counter_t interval) noexcept;

        /// @brief Returns the remaining decision budget before the next scheduled restart.
        /// @return Remaining decision budget.
        /// @throws None (noexcept).
        counter_t current_decision_restart_budget() const noexcept;

        /// @brief Returns how many conflicts have been observed by this controller.
        /// @return Total conflict count.
        /// @throws None (noexcept).
        counter_t conflict_count() const noexcept { return conflict_count_; }

        /// @brief Returns how many decisions have been observed by this controller.
        /// @return Total decision count.
        /// @throws None (noexcept).
        counter_t decision_count() const noexcept { return decision_count_; }

        /// @brief Returns how many restarts have been performed.
        /// @return Total restart count.
        /// @throws None (noexcept).
        counter_t restart_count() const noexcept { return restart_count_; }

    private:
        /// @brief Returns the `index`-th Luby multiplier (1, 1, 2, 1, 1, 2, 4, ...).
        /// @details Restart intervals must grow without bound, otherwise a fixed cadence can prevent the search
        /// from ever completing: with a short interval the solver restarts before it can accumulate the learned
        /// clauses that drive progress, and an unsatisfiable instance is never closed out. The Luby sequence gives
        /// frequent early restarts and unboundedly growing later ones, which preserves completeness.
        /// @param index Zero-based position in the sequence.
        /// @return Multiplier applied to the configured base interval.
        /// @throws None (noexcept).
        static counter_t luby_multiplier(counter_t index) noexcept;

        /// @brief Returns the conflict budget for the next restart, growing along the Luby sequence.
        /// @throws None (noexcept).
        counter_t next_restart_budget() noexcept { return restart_interval_ * luby_multiplier(restart_sequence_index_++); }

        /// @brief Returns the decision budget for the next restart, growing along the same sequence.
        /// @details The decision trigger needs the growth just as much as the conflict trigger: an interval of one
        /// makes a restart due after every decision, and a search that restarts to level zero then undoes exactly
        /// the decision it just made, forever, without ever reaching a conflict.
        /// @throws None (noexcept).
        counter_t next_decision_restart_budget() noexcept
        {
            return decision_restart_interval_ * luby_multiplier(decision_restart_sequence_index_++);
        }

        /// @brief Re-anchors both scheduled triggers to the current counters.
        /// @throws None (noexcept).
        void rebase_schedule() noexcept;

        static constexpr double fast_glue_alpha_ {0.5};
        static constexpr double slow_glue_alpha_ {0.95};
        static constexpr counter_t glue_restart_warmup_ {4u};
        counter_t conflict_count_ {};
        counter_t decision_count_ {};
        counter_t restart_count_ {};
        counter_t restart_sequence_index_ {};
        counter_t conflicts_at_last_restart_ {};
        counter_t decision_restart_sequence_index_ {};
        counter_t restart_interval_ {};
        counter_t next_scheduled_restart_at_ {};
        counter_t decision_restart_interval_ {};
        counter_t next_scheduled_decision_restart_at_ {};
        bool restart_pending_ {};
        double fast_glue_ema_ {};
        double slow_glue_ema_ {};
        double glue_restart_threshold_ {};
        counter_t glue_observation_count_ {};
    };
}
