/// @file api/kmx/sat/solve_request.hpp
/// @brief Immutable aggregate for assumptions, limits, proof/checking flags, simplification options, and incremental mode
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/counter.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat
{
    /// @brief Immutable aggregate for assumptions, limits, proof/checking flags, simplification options, and incremental mode
    /// for one search episode.
    ///
    /// A `solve_request` is the single input object handed to `solver::solve` and, through it, to
    /// `search_coordinator::run_search_epoch`; it is treated as read-only for the whole episode so that the
    /// assumptions, limits, and pass selection observed by `external_frontend`, `assumption_store`, and the
    /// simplification schedulers never change mid-episode. Every field must satisfy the runtime-limits and
    /// options-input validation rules (non-negative, overflow-safe limits; assumption literals validated through
    /// `variable_mapper` before entering the CDCL loop) before the episode may start.
    struct solve_request final
    {
        /// @brief Assumption literals for this episode, validated and applied before search begins and strictly
        /// separated from the permanent clause database, per the incremental input-validation rules.
        std::vector<literal> assumptions {};
        /// @brief Maximum number of conflicts allowed before the episode reports an unknown/terminated status; zero
        /// means unlimited.
        counter_t conflict_limit {};
        /// @brief Maximum number of decisions allowed before the episode reports an unknown/terminated status; zero
        /// means unlimited.
        counter_t decision_limit {};
        /// @brief Bitmask selecting which preprocessing/inprocessing passes `preprocess_scheduler` and
        /// `inprocess_scheduler` are permitted to run during this episode.
        std::uint64_t enabled_pass_mask {};
        /// @brief When true, any malformed or out-of-domain input encountered during this episode is rejected
        /// immediately (`strict` parse-mode policy); when false, only benign metadata irregularities may be
        /// tolerated and semantic corruption is still never accepted.
        bool strict_mode {};
    };
}
