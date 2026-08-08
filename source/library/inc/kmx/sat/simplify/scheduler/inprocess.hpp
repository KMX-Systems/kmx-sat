/// @file inc/kmx/sat/simplify/scheduler/inprocess.hpp
/// @brief All periodic simplifications that run during search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::simplify::scheduler
{
    /// @brief All periodic simplifications that run during search.
    ///
    /// Unlike `scheduler::preprocess`, `scheduler::inprocess` runs repeatedly, interleaved with CDCL search epochs
    /// (the Kissat/CaDiCaL "inprocessing" style): `should_run` decides, based on conflict count and
    /// `telemetry::ema_tracker` signals, whether it is time for another simplification epoch;
    /// `compute_budget` bounds how much work (time/conflicts-equivalent) the epoch may spend, since inprocessing
    /// competes with search for the same conflict budget; `run_pass_sequence` executes the currently enabled subset
    /// of passes, orchestrated by `run_epoch`; `record_effectiveness` reports the epoch's yield (clauses removed,
    /// variables eliminated) to `preprocessing_profile_selector` and `telemetry::solver_statistics`.
    /// `controller::restart::reset_after_inprocess` must be called after an epoch changes the clause set, since
    /// restart interval counters accumulated before the epoch are no longer meaningful.
    /// @note Like `scheduler::preprocess`, this scheduler must consult `proof::proof_manager` for the active proof
    /// format(s) before running a format-sensitive pass, and `memory_governor`'s hard-ceiling escalation may suspend
    /// this scheduler entirely for the remainder of the current `solve()` call.
    class inprocess final
    {
    public:
        /// @brief Constructs an inprocess scheduler with a default epoch schedule.
        /// @throws None (noexcept).
        inprocess() noexcept = default;

        /// @brief Checks whether an inprocessing epoch is due now.
        /// @return True if an epoch should run.
        /// @throws None (noexcept).
        bool should_run() const noexcept
        {
            return false;
        }

        /// @brief Runs one inprocessing epoch within its computed budget.
        /// @throws None (noexcept).
        void run_epoch() noexcept
        {
        }

        /// @brief Computes the work budget available for the next inprocessing epoch.
        /// @return Budget value, in an implementation-defined conflict-equivalent unit.
        /// @throws None (noexcept).
        std::uint64_t compute_budget() const noexcept
        {
            return {};
        }

        /// @brief Executes the currently enabled sequence of inprocessing passes for one epoch.
        /// @throws None (noexcept).
        void run_pass_sequence() noexcept
        {
        }

        /// @brief Records the effectiveness of the last epoch for future scheduling decisions.
        /// @throws None (noexcept).
        void record_effectiveness() noexcept
        {
        }
    };
}
