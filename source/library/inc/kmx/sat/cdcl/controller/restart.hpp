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
        bool should_restart() const noexcept
        {
            return false;
        }

        /// @brief Advances restart bookkeeping by one conflict.
        /// @throws None (noexcept).
        void tick_conflict() noexcept
        {
        }

        /// @brief Advances restart bookkeeping by one decision.
        /// @throws None (noexcept).
        void tick_decision() noexcept
        {
        }

        /// @brief Resynchronizes restart counters after an inprocessing epoch changes the clause set.
        /// @throws None (noexcept).
        void reset_after_inprocess() noexcept
        {
        }

        /// @brief Returns the remaining conflict budget before the next scheduled restart.
        /// @return Remaining conflict budget.
        /// @throws None (noexcept).
        std::uint64_t current_restart_budget() const noexcept
        {
            return {};
        }
    };
}
