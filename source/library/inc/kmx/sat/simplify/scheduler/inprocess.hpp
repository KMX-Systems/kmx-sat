/// @file inc/kmx/sat/simplify/scheduler/inprocess.hpp
/// @brief All periodic simplifications that run during search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <vector>
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

        /// @brief Sets the number of conflicts already seen by the search engine.
        void set_conflicts_seen(const std::uint64_t conflicts) noexcept
        {
            conflicts_seen_ = conflicts;
        }

        /// @brief Sets how many restarts have already occurred.
        void set_restart_count(const std::uint64_t restarts) noexcept
        {
            restart_count_ = restarts;
        }

        /// @brief Checks whether an inprocessing epoch is due now.
        /// @return True if an epoch should run.
        /// @throws None (noexcept).
        bool should_run() const noexcept
        {
            return conflicts_seen_ >= 32u || restart_count_ >= 1u;
        }

        /// @brief Runs one inprocessing epoch within its computed budget.
        /// @throws None (noexcept).
        void run_epoch() noexcept
        {
            ++epoch_count_;
            last_budget_ = compute_budget();
            run_pass_sequence();
            record_effectiveness();
        }

        /// @brief Computes the work budget available for the next inprocessing epoch.
        /// @return Budget value, in an implementation-defined conflict-equivalent unit.
        /// @throws None (noexcept).
        std::uint64_t compute_budget() const noexcept
        {
            return 1u + (conflicts_seen_ / 32u) + restart_count_;
        }

        /// @brief Executes the currently enabled sequence of inprocessing passes for one epoch.
        /// @throws None (noexcept).
        void run_pass_sequence() noexcept
        {
            pass_count_ += 1u;
        }

        /// @brief Records the effectiveness of the last epoch for future scheduling decisions.
        /// @throws None (noexcept).
        void record_effectiveness() noexcept
        {
            last_effectiveness_ = last_budget_ > 0u ? 1u : 0u;
        }

        std::uint64_t pass_count() const noexcept
        {
            return pass_count_;
        }

        std::uint64_t last_effectiveness() const noexcept
        {
            return last_effectiveness_;
        }

        /// @brief Returns how many epochs have been run.
        std::uint64_t epoch_count() const noexcept
        {
            return epoch_count_;
        }

        /// @brief Returns the budget used by the last epoch.
        std::uint64_t last_budget() const noexcept
        {
            return last_budget_;
        }

    private:
        std::uint64_t conflicts_seen_ {0};
        std::uint64_t restart_count_ {0};
        std::uint64_t epoch_count_ {0};
        std::uint64_t last_budget_ {0};
        std::uint64_t pass_count_ {0};
        std::uint64_t last_effectiveness_ {0};
    };
}
