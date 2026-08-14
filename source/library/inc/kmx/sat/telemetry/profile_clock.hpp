/// @file inc/kmx/sat/telemetry/profile_clock.hpp
/// @brief Phase profiling and predictable accounting.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <chrono>
    #include <cstdint>
    #include <ctime>
    #include <string>
    #include <string_view>
    #include <utility>
    #include <vector>
#endif

namespace kmx::sat::telemetry
{
    /// @brief Phase profiling and predictable accounting.
    ///
    /// @details
    /// `profile_clock` measures wall-clock and process (CPU) time spent in named phases (parsing, preprocessing,
    /// search, proof checking) so `report_formatter`/`solver_statistics` can attribute time accurately and so the
    /// comparative benchmarking harness can compare phase-level timing against pinned CaDiCaL/Kissat releases.
    /// `start_phase`/`stop_phase` bracket one named phase (phases may nest or repeat); `current_process_time`/
    /// `current_wall_time` expose cumulative totals independent of any in-progress phase, giving predictable
    /// accounting even if a phase is stopped out of order or omitted.
    class profile_clock final
    {
    public:
        /// @brief Constructs a profile clock with no phases started.
        /// @throws None (noexcept).
        profile_clock() noexcept = default;

        /// @brief Starts timing a named phase.
        /// @param phase_name Identifier of the phase being started.
        /// @throws None (noexcept).
        void start_phase(const std::string_view phase_name) noexcept
        {
            active_phases_.push_back({std::string {phase_name}, std::chrono::steady_clock::now(), std::clock()});
        }

        /// @brief Stops timing a named phase, accumulating its elapsed time.
        /// @param phase_name Identifier of the phase being stopped.
        /// @throws None (noexcept).
        void stop_phase(const std::string_view phase_name) noexcept
        {
            for (auto it = active_phases_.rbegin(); it != active_phases_.rend(); ++it)
            {
                if (it->name == phase_name)
                {
                    const auto wall_now = std::chrono::steady_clock::now();
                    const auto process_now = std::clock();
                    const auto wall_elapsed_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(wall_now - it->wall_start);
                    const auto process_elapsed_ticks = process_now >= it->process_start ? process_now - it->process_start : 0;
                    const auto process_elapsed_ms = std::chrono::milliseconds {
                        static_cast<std::int64_t>(process_elapsed_ticks * 1000 / CLOCKS_PER_SEC)};
                    const auto wall_ticks = std::max<std::int64_t>(1, wall_elapsed_ms.count());
                    const auto process_ticks = std::max<std::int64_t>(1, process_elapsed_ms.count());

                    cumulative_wall_time_ += static_cast<std::uint64_t>(wall_ticks);
                    cumulative_process_time_ += static_cast<std::uint64_t>(process_ticks);
                    active_phases_.erase(std::next(it).base());
                    break;
                }
            }
        }

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
            std::string name {};
            std::chrono::steady_clock::time_point wall_start {};
            std::clock_t process_start {};
        };

        std::vector<phase_entry> active_phases_ {};
        std::uint64_t cumulative_wall_time_ {};
        std::uint64_t cumulative_process_time_ {};
    };
}
