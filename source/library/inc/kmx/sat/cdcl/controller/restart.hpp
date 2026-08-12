/// @file inc/kmx/sat/cdcl/controller/restart.hpp
/// @brief Reluctant doubling, conflict intervals, and EMA-based triggers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::cdcl::controller
{
    /// @brief Reluctant doubling, conflict intervals, and EMA-based triggers.
    ///
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

        /// @brief Advances restart bookkeeping by one conflict.
        /// @throws None (noexcept).
        void tick_conflict() noexcept
        {
            ++conflict_count_;

            if (restart_interval_ == 0)
            {
                return;
            }

            if (conflict_count_ >= next_scheduled_restart_at_)
            {
                restart_pending_ = true;
                next_scheduled_restart_at_ = conflict_count_ + restart_interval_;
            }
        }

        /// @brief Advances restart bookkeeping by one decision.
        /// @throws None (noexcept).
        void tick_decision() noexcept
        {
            ++decision_count_;

            if (decision_restart_interval_ == 0)
            {
                return;
            }

            if (decision_count_ >= next_scheduled_decision_restart_at_)
            {
                restart_pending_ = true;
                next_scheduled_decision_restart_at_ = decision_count_ + decision_restart_interval_;
            }
        }

        /// @brief Resynchronizes restart counters after an inprocessing epoch changes the clause set.
        /// @throws None (noexcept).
        void reset_after_inprocess() noexcept
        {
            if (restart_pending_)
            {
                ++restart_count_;
            }
            restart_pending_ = false;

            if (restart_interval_ != 0)
            {
                next_scheduled_restart_at_ = conflict_count_ + restart_interval_;
            }

            if (decision_restart_interval_ != 0)
            {
                next_scheduled_decision_restart_at_ = decision_count_ + decision_restart_interval_;
            }
        }

        /// @brief Returns the remaining conflict budget before the next scheduled restart.
        /// @return Remaining conflict budget.
        /// @throws None (noexcept).
        std::uint64_t current_restart_budget() const noexcept
        {
            if (restart_interval_ == 0)
            {
                return 0;
            }

            if (restart_pending_)
            {
                return 0;
            }

            if (conflict_count_ >= next_scheduled_restart_at_)
            {
                return 0;
            }

            return next_scheduled_restart_at_ - conflict_count_;
        }

        /// @brief Forces `should_restart` to fire until the next `reset_after_inprocess`.
        /// @throws None (noexcept).
        void request_restart() noexcept { restart_pending_ = true; }

        /// @brief Sets the periodic conflict interval used for scheduled restart triggers.
        /// @param interval Number of conflicts between scheduled restart opportunities (zero disables schedule).
        /// @throws None (noexcept).
        void set_restart_interval(const std::uint64_t interval) noexcept
        {
            restart_interval_ = interval;
            next_scheduled_restart_at_ = conflict_count_ + restart_interval_;
        }

        /// @brief Sets a periodic decision interval used for scheduled restart triggers.
        /// @param interval Number of decisions between restart opportunities (zero disables decision schedule).
        /// @throws None (noexcept).
        void set_decision_restart_interval(const std::uint64_t interval) noexcept
        {
            decision_restart_interval_ = interval;
            next_scheduled_decision_restart_at_ = decision_count_ + decision_restart_interval_;
        }

        /// @brief Returns the remaining decision budget before the next scheduled restart.
        /// @return Remaining decision budget.
        /// @throws None (noexcept).
        std::uint64_t current_decision_restart_budget() const noexcept
        {
            if (decision_restart_interval_ == 0)
            {
                return 0;
            }

            if (restart_pending_)
            {
                return 0;
            }

            if (decision_count_ >= next_scheduled_decision_restart_at_)
            {
                return 0;
            }

            return next_scheduled_decision_restart_at_ - decision_count_;
        }

        /// @brief Returns how many conflicts have been observed by this controller.
        /// @return Total conflict count.
        /// @throws None (noexcept).
        std::uint64_t conflict_count() const noexcept { return conflict_count_; }

        /// @brief Returns how many decisions have been observed by this controller.
        /// @return Total decision count.
        /// @throws None (noexcept).
        std::uint64_t decision_count() const noexcept { return decision_count_; }

        /// @brief Returns how many restarts have been performed.
        /// @return Total restart count.
        /// @throws None (noexcept).
        std::uint64_t restart_count() const noexcept { return restart_count_; }

    private:
        std::uint64_t conflict_count_ {0};
        std::uint64_t decision_count_ {0};
        std::uint64_t restart_count_ {0};
        std::uint64_t restart_interval_ {0};
        std::uint64_t next_scheduled_restart_at_ {0};
        std::uint64_t decision_restart_interval_ {0};
        std::uint64_t next_scheduled_decision_restart_at_ {0};
        bool restart_pending_ {false};
    };
}
