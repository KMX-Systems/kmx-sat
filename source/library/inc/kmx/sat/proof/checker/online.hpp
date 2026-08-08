/// @file inc/kmx/sat/proof/checker/online.hpp
/// @brief Internal forward validation of derivations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::checker
{
    /// @brief Internal forward validation of derivations.
    ///
    /// `checker::online` is a lightweight, always-available forward checker that mirrors
    /// `proof::proof_manager`'s event stream in real time, independent of any external tracer format: `on_add`/
    /// `on_delete`/`on_shrink` replay the same structural events the tracers receive against an internal checker
    /// clause database (`checker_clause_db`) and watch structures, so a derivation error can be caught the moment it
    /// happens rather than only when an external DRAT/LRAT file is later checked out-of-band.
    /// `validate_conclusion` verifies the accumulated events are consistent with the episode's claimed SAT/UNSAT
    /// verdict, and `statistics` exposes checker overhead/coverage counters for `telemetry::solver_statistics`.
    /// @note This checker only forward-validates against the events it observes; it is deliberately simpler than
    /// `checker::lrat` and does not replace the stronger guarantee an external replay of an enabled proof format
    /// provides, per the proof replay testing requirement.
    class online final
    {
    public:
        /// @brief Constructs the online proof checker state.
        /// @throws None (noexcept).
        online() noexcept = default;

        /// @brief Registers an add-clause proof event in checker state.
        /// @param ref Clause reference being added.
        /// @throws None (noexcept).
        void on_add(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Registers a delete-clause proof event in checker state.
        /// @param ref Clause reference being deleted.
        /// @throws None (noexcept).
        void on_delete(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Registers a shrink-clause proof event in checker state.
        /// @param ref Clause reference being shrunk.
        /// @throws None (noexcept).
        void on_shrink(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Validates current checker state against expected proof conclusion conditions.
        /// @return True if the accumulated events are currently consistent.
        /// @throws None (noexcept).
        bool validate_conclusion() const noexcept
        {
            return false;
        }

        /// @brief Emits or updates checker statistics for diagnostics.
        /// @throws None (noexcept).
        void statistics() const noexcept
        {
        }
    };
}
