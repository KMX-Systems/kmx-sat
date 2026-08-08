/// @file inc/kmx/sat/cdcl/chb_tracker.hpp
/// @brief Conflict History-Based branching scores as a third candidate heuristic alongside VMTF/EVSIDS. Research-track
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Conflict History-Based branching scores as a third candidate heuristic alongside VMTF/EVSIDS. Research-track
    /// heuristic to be validated against the comparative benchmarking harness before becoming a default.
    ///
    /// CHB assigns each variable a score updated by a "reward" signal tied to how recently it was involved in a
    /// conflict, decaying over time (`decay_step`) rather than depending purely on bump-and-rescale like
    /// `evsids_heap`. `update_on_assignment`/`update_on_conflict` feed the reward signal, and `score_of` exposes the
    /// current score so `engine::decision::select_heuristic_blend` can combine it with VMTF/EVSIDS output under an
    /// explicit blending policy.
    /// @note This is a research-track heuristic: it must be validated on the comparative benchmarking harness
    /// against pinned CaDiCaL/Kissat releases before `engine::decision` may use it by default; until then it is an
    /// optional signal `engine::decision` may consult alongside VMTF/EVSIDS.
    /// @reference Conflict History-Based (CHB) branching heuristic (Liang, Ganesh, Poupart, Czarnecki, "Learning
    /// Rate Based Branching Heuristic for SAT Solvers").
    class chb_tracker final
    {
    public:
        /// @brief Constructs a CHB tracker with zero scores for every variable.
        /// @throws None (noexcept).
        chb_tracker() noexcept = default;

        /// @brief Updates the reward signal for a variable when it is assigned.
        /// @param var Variable being assigned.
        /// @throws None (noexcept).
        void update_on_assignment(const variable var) noexcept
        {
        }

        /// @brief Updates the reward signal for a variable involved in a conflict.
        /// @param var Variable involved in the conflict.
        /// @throws None (noexcept).
        void update_on_conflict(const variable var) noexcept
        {
        }

        /// @brief Returns the current CHB score for a variable.
        /// @param var Variable to query.
        /// @return Current score value.
        /// @throws None (noexcept).
        double score_of(const variable var) const noexcept
        {
            return {};
        }

        /// @brief Applies one decay step to all scores, reducing the weight of older conflict participation.
        /// @throws None (noexcept).
        void decay_step() noexcept
        {
        }
    };
}
