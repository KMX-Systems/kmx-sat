/// @file api/kmx/sat/counter.hpp
/// @brief Shared type for solver event counters and limits.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat
{
    /// @brief Wide counter type for solver events and per-episode limits.
    /// @details This remains 64-bit so long-running incremental sessions cannot overflow at 2^32 events.
    using counter_t = std::uint64_t;
}
