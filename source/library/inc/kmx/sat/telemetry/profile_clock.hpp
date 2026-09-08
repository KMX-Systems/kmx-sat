/// @file inc/kmx/sat/telemetry/profile_clock.hpp
/// @brief Phase profiling and predictable accounting.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <chrono>
    #include <cstdint>
    #include <ctime>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/telemetry/phase_id.hpp>

namespace kmx::sat::telemetry
{
    /// @brief Phase profiling and predictable accounting.
    /// @details
    /// `profile_clock` measures wall-clock and process (CPU) time spent in each `phase_id` (parsing,
    /// preprocessing, search, proof checking) so `report_formatter`/`solver_statistics` can attribute time accurately and so the
    /// comparative benchmarking harness can compare phase-level timing against pinned CaDiCaL/Kissat releases.
    /// `start_phase`/`stop_phase` bracket one phase (phases may nest or repeat); `current_process_time`/
    /// `current_wall_time` expose cumulative totals independent of any in-progress phase, giving predictable
    /// accounting even if a phase is stopped out of order or omitted.
    class profile_clock final
    {
    public:
        /// @brief Constructs a profile clock with no phases started.
        /// @throws None (noexcept).
        profile_clock() noexcept = default;

        /// @brief Starts timing one phase.
        /// @param id Phase being started.
        /// @throws None (noexcept).
        void start_phase(const phase_id id) noexcept { active_phases_.push_back({id, std::chrono::steady_clock::now(), std::clock()}); }

        /// @brief Stops timing one phase, accumulating its elapsed time.
        /// @param id Phase being stopped.
        /// @throws None (noexcept).
        void stop_phase(const phase_id id) noexcept;

        /// @brief Returns the cumulative process (CPU) time consumed so far.
        /// @return Process time, in an implementation-defined unit.
        /// @throws None (noexcept).
        std::uint64_t current_process_time() const noexcept { return cumulative_process_time_; }

        /// @brief Returns the cumulative wall-clock time elapsed so far.
        /// @return Wall-clock time, in an implementation-defined unit.
        /// @throws None (noexcept).
        std::uint64_t current_wall_time() const noexcept { return cumulative_wall_time_; }

        std::size_t phase_count() const noexcept { return active_phases_.size(); }

    private:
        struct phase_entry
        {
            phase_id id {};
            std::chrono::steady_clock::time_point wall_start {};
            std::clock_t process_start {};
        };

        std::vector<phase_entry> active_phases_ {};
        std::uint64_t cumulative_wall_time_ {};
        std::uint64_t cumulative_process_time_ {};
    };
}
