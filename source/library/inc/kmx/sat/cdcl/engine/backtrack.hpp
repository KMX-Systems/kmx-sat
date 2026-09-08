/// @file inc/kmx/sat/cdcl/engine/backtrack.hpp
/// @brief Correct state unwind after conflict or restart.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/cdcl/stack/decision_frame.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/trail.hpp>

namespace kmx::sat::cdcl::engine
{
    /// @brief Correct state unwind after conflict or restart.
    /// @details
    /// `engine::backtrack` is the only subsystem allowed to unwind `trail`/`stack::decision_frame`/`store::assignment`
    /// state together, ensuring the three stay consistent. `backtrack_to_level` performs a standard non-chronological
    /// backjump to the level computed by `conflict_analyzer::compute_backjump_level`, truncating the trail via
    /// `trail::pop_to` and clearing assignments through `store::assignment::unassign_from` for every variable above
    /// that level. `chronological_backtrack` implements the alternative Kissat-style chronological backtracking mode
    /// (unwind to `current_level - 1` regardless of the computed backjump level) used when non-chronological jumps
    /// are disabled or inapplicable. `reuse_trail` avoids fully rebuilding frame/trail metadata when a suffix of the
    /// trail remains valid across the jump, informed by `assumption_reuse_advisor` in incremental workloads.
    /// `clear_transient_marks` clears `store::assignment` analysis-seen bits left over from the conflict that
    /// triggered this backtrack.
    class backtrack final
    {
    public:
        /// @brief Constructs a backtrack engine with no pending unwind operation.
        /// @throws None (noexcept).
        backtrack() noexcept = default;

        /// @brief Binds this engine to the mutable CDCL state it should rewind.
        /// @param trail_state Mutable trail that will be truncated.
        /// @param assignment Mutable assignment store that will have assignments above the target level removed.
        /// @param decision_frames Mutable decision-frame stack that will be truncated.
        /// @throws None (noexcept).
        void attach_state(trail& trail_state, store::assignment& assignment, stack::decision_frame& decision_frames) noexcept;

        /// @brief Performs a non-chronological backjump to the given decision level.
        /// @param level Target decision level to unwind to.
        /// @throws None (noexcept).
        void backtrack_to_level(const std::uint32_t level) noexcept;

        /// @brief Performs a chronological backtrack of exactly one decision level.
        /// @throws None (noexcept).
        void chronological_backtrack() noexcept;

        /// @brief Reuses trail/frame metadata for a suffix of the trail that remains valid across the jump.
        /// @throws None (noexcept).
        void reuse_trail() noexcept;

        /// @brief Clears transient analysis marks left over from the conflict that triggered this backtrack.
        /// @throws None (noexcept).
        void clear_transient_marks() noexcept
        {
            if (assignment_ != nullptr)
                assignment_->clear_analysis_marks();
        }

        /// @brief Returns the most recent decision level requested by this backtrack engine.
        /// @return Last target level passed to `backtrack_to_level`.
        std::uint32_t last_backtracked_level() const noexcept { return last_backtracked_level_; }

        /// @brief Returns how many times the trail metadata reuse path was requested.
        /// @return Number of reuse requests.
        /// @throws None (noexcept).
        std::uint32_t trail_reuse_count() const noexcept { return trail_reuse_count_; }

    private:
        trail* trail_state_ {};
        store::assignment* assignment_ {};
        stack::decision_frame* decision_frames_ {};
        std::uint32_t last_backtracked_level_ {};
        std::uint32_t trail_reuse_count_ {};
    };
}
