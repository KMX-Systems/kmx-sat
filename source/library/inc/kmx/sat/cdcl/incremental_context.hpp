/// @file inc/kmx/sat/cdcl/incremental_context.hpp
/// @brief Declares exactly what survives between two solve() calls.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::cdcl
{
    /// @brief Declares exactly what survives between two solve() calls.
    ///
    /// Incremental SAT+UNSAT semantics require an explicit boundary between per-episode transient state (assumptions,
    /// temporary constraint, decision trail above level zero) and state that must persist across episodes (learned
    /// clauses, external variable mapping, a caller-selected subset of options). `begin_solve_epoch`/`end_solve_epoch`
    /// bracket one `solver::solve` call; `retain_learned_clause` marks clauses from the just-finished episode that
    /// remain valid and useful for the next one; `reset_transient_state` discards everything scoped to the episode
    /// that just ended; `persist_option_subset` carries forward only the options explicitly designated as persistent
    /// across episodes. `compaction_service` and `garbage_collector` must not invalidate the semantics this class
    /// establishes: a clause retained here must still resolve correctly after compaction or GC in a later epoch.
    class incremental_context final
    {
    public:
        /// @brief Constructs an incremental context with no epoch currently open.
        /// @throws None (noexcept).
        incremental_context() noexcept = default;

        /// @brief Opens a new solve epoch, establishing the boundary for transient-state tracking.
        /// @throws None (noexcept).
        void begin_solve_epoch() noexcept
        {
            in_epoch_ = true;
            retained_learned_clauses_ = 0u;
        }

        /// @brief Closes the current solve epoch, finalizing which state is retained versus discarded.
        /// @throws None (noexcept).
        void end_solve_epoch() noexcept
        {
            in_epoch_ = false;
        }

        /// @brief Marks a learned clause from the just-finished episode as eligible to persist into the next epoch.
        /// @throws None (noexcept).
        void retain_learned_clause() noexcept
        {
            if (in_epoch_)
            {
                retained_learned_clauses_ += 1u;
            }
        }

        /// @brief Discards all state scoped strictly to the episode that just ended (assumptions, temporary
        /// constraint, transient trail above the base level).
        /// @throws None (noexcept).
        void reset_transient_state() noexcept
        {
            retained_learned_clauses_ = 0u;
        }

        /// @brief Carries forward only the subset of options explicitly designated as persistent across episodes.
        /// @throws None (noexcept).
        void persist_option_subset() noexcept
        {
            persisted_option_subset_ = true;
        }

        bool in_epoch() const noexcept
        {
            return in_epoch_;
        }

        std::uint32_t retained_learned_clauses() const noexcept
        {
            return retained_learned_clauses_;
        }

    private:
        bool in_epoch_ {false};
        bool persisted_option_subset_ {false};
        std::uint32_t retained_learned_clauses_ {0u};
    };
}
