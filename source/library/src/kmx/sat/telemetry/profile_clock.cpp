/// @file library/src/kmx/sat/telemetry/profile_clock.cpp
/// @brief Out-of-line definitions declared by kmx/sat/telemetry/profile_clock.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/telemetry/profile_clock.hpp>

namespace kmx::sat::telemetry
{
    void profile_clock::stop_phase(const phase_id id) noexcept
    {
        for (auto it = active_phases_.rbegin(); it != active_phases_.rend(); ++it)
        {
            if (it->id == id)
            {
                const auto wall_now = std::chrono::steady_clock::now();
                const auto process_now = std::clock();
                const auto wall_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wall_now - it->wall_start);
                const auto process_elapsed_ticks = (process_now >= it->process_start) ? process_now - it->process_start : 0L;
                const auto process_elapsed_ms =
                    std::chrono::milliseconds {static_cast<std::int64_t>(process_elapsed_ticks * 1000L / CLOCKS_PER_SEC)};
                const auto wall_ticks = std::max<std::int64_t>(1L, wall_elapsed_ms.count());
                const auto process_ticks = std::max<std::int64_t>(1L, process_elapsed_ms.count());

                cumulative_wall_time_ += static_cast<std::uint64_t>(wall_ticks);
                cumulative_process_time_ += static_cast<std::uint64_t>(process_ticks);
                active_phases_.erase(std::next(it).base());
                break;
            }
        }
    }
}
