/// @file inc/kmx/sat/telemetry/phase_id.hpp
/// @brief Closed set naming the solver phases that are timed and summarized separately.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::telemetry
{
    /// @brief Solver phase whose wall/process time and summary line are accounted separately.
    /// @details `profile_clock` brackets a phase with `start_phase`/`stop_phase` and `logging_facade` labels a
    /// phase-summary event with the same identity, so both report against one closed set rather than free text.
    enum class phase_id : std::uint8_t
    {
        /// @brief Reading and building the formula from its input source.
        parse,
        /// @brief One-time simplification pipeline running before the first search epoch.
        preprocess,
        /// @brief CDCL search itself.
        search,
        /// @brief Periodic simplification epoch interleaved with search.
        inprocess,
        /// @brief Proof emission and checking.
        proof_check,
    };
}
