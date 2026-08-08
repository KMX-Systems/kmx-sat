/// @file inc/kmx/sat/cdcl/controller/reduce.hpp
/// @brief Learned-clause database management based on glue, activity, and usage.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/database.hpp>

namespace kmx::sat::cdcl::controller
{
    /// @brief Learned-clause database management based on glue, activity, and usage.
    ///
    /// `controller::reduce` periodically shrinks `clause::database`'s redundant (learned) clause set so it does not
    /// grow without bound, following the glue/activity-based retention policy common to CaDiCaL/Kissat: low-glue,
    /// recently-used clauses are kept, while high-glue, long-unused clauses become deletion candidates.
    /// `should_reduce` decides, from a conflict-count schedule, when a reduction pass is due;
    /// `select_reduction_candidates` ranks redundant clauses by glue/activity/`clause::header::used_count`;
    /// `reduce` marks the selected candidates garbage via `clause::database::mark_garbage` (never a clause currently
    /// serving as a reason, per `clause::header::reason`); `flush_redundant` performs the actual removal, typically
    /// in coordination with `flush_restore_manager`; and `update_tiers` recomputes tier membership after the pass.
    /// `memory_governor` may also invoke this controller out of its normal schedule as the first step of its
    /// soft-ceiling escalation ladder.
    class reduce final
    {
    public:
        /// @brief Constructs a reduce controller with a default schedule.
        /// @throws None (noexcept).
        reduce() noexcept = default;

        /// @brief Checks whether the reduction schedule (or an out-of-band memory-pressure request) is due.
        /// @return True if a reduction pass should run now.
        /// @throws None (noexcept).
        bool should_reduce() const noexcept
        {
            return false;
        }

        /// @brief Ranks redundant clauses by glue/activity/usage to select reduction candidates.
        /// @throws None (noexcept).
        void select_reduction_candidates() noexcept
        {
        }

        /// @brief Marks the selected low-quality redundant clauses as garbage.
        /// @throws None (noexcept).
        void reduce() noexcept
        {
        }

        /// @brief Physically removes clauses marked garbage by this reduction pass.
        /// @throws None (noexcept).
        void flush_redundant() noexcept
        {
        }

        /// @brief Recomputes clause-database tier membership after a reduction pass.
        /// @throws None (noexcept).
        void update_tiers() noexcept
        {
        }
    };
}
