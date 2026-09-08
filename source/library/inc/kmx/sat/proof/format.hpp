/// @file inc/kmx/sat/proof/format.hpp
/// @brief Closed sets naming the proof output formats and the internal checkers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::proof
{
    /// @brief Proof output format emitted by a concrete tracer.
    /// @details Enumerator order matches the alternative order of `tracer::variant_t`, so the format of a bound
    /// `tracer::view` is its variant index; `tracer::view` static-asserts that correspondence.
    enum class format_id : std::uint8_t
    {
        /// @brief Clausal DRAT proof.
        drat,
        /// @brief Linear RAT proof with explicit antecedent hints.
        lrat,
        /// @brief Framework RAT proof carrying both original and derived clause identities.
        frat,
        /// @brief Incremental DRUP proof for incremental solving.
        idrup,
        /// @brief Linear incremental DRUP proof.
        lidrup,
        /// @brief Pseudo-Boolean VeriPB proof.
        veripb,
    };

    /// @brief Internal checker validating emitted events during the solve itself.
    enum class checker_id : std::uint8_t
    {
        /// @brief Online RUP checker running alongside the search.
        online,
        /// @brief LRAT antecedent-chain checker.
        lrat,
    };
}
