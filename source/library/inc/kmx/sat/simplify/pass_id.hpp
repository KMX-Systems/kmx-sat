/// @file inc/kmx/sat/simplify/pass_id.hpp
/// @brief Closed set naming every simplification pass the schedulers can run.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::simplify
{
    /// @brief One simplification pass, as scheduled by `scheduler::preprocess` and `scheduler::inprocess`.
    /// @details The enumerator value is also the pass's bit position in an enabled-pass mask
    /// (`scheduler::preprocess::pass_bit`, `solve_request::enabled_pass_mask`), so enumerator order is part of that
    /// mask's contract and must not be reordered.
    enum class pass_id : std::uint8_t
    {
        transitive_reducer,
        decomposition,
        probing,
        forward_subsumer,
        blocked,
        covered,
        bounded,
        fast,
        instantiation,
        factorizer,
        gate,
        congruence,
        vivifier,
        sweep,
    };
}
