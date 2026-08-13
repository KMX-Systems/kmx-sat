/// @file inc/kmx/sat/proof/checker/online.hpp
/// @brief Internal forward validation of derivations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <unordered_set>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::checker
{
    /// @brief Internal forward validation of derivations.
    ///
    /// @details
    /// `checker::online` is a lightweight, always-available forward checker that mirrors
    /// `proof::proof_manager`'s event stream in real time, independent of any external tracer format: `on_add`/
    /// `on_delete`/`on_shrink` replay the same structural events the tracers receive against an internal active-
    /// reference set, so a derivation error can be caught the moment it happens rather than only when an external
    /// DRAT/LRAT file is later checked out-of-band.
    /// `validate_conclusion` verifies the accumulated events are consistent with the episode's claimed SAT/UNSAT
    /// verdict, and `coverage_snapshot` exposes checker overhead/coverage counters for `telemetry::solver_statistics`.
    /// @note This checker only forward-validates *structural* consistency (no double-add, no delete/shrink of an
    /// unknown reference); it is deliberately simpler than `checker::lrat`, which additionally verifies antecedent
    /// chains, and does not replace the stronger guarantee an external replay of an enabled proof format provides,
    /// per the proof replay testing requirement.
    class online final
    {
    public:
        /// @brief Coverage/overhead counters surfaced to `telemetry::solver_statistics`.
        struct coverage final
        {
            std::size_t clauses_added {};
            std::size_t clauses_deleted {};
            std::size_t clauses_shrunk {};
            std::size_t structural_errors {};
        };

        /// @brief Constructs the online proof checker state.
        /// @throws None (noexcept).
        online() noexcept = default;

        /// @brief Registers an add-clause proof event in checker state.
        /// @param ref Clause reference being added.
        /// @throws None (noexcept).
        void on_add(const cdcl::clause::ref_t ref) noexcept
        {
            if (!ref.valid() || !active_.insert(ref.offset()).second)
            {
                ++coverage_.structural_errors;
                return;
            }
            ++coverage_.clauses_added;
        }

        /// @brief Registers a delete-clause proof event in checker state.
        /// @param ref Clause reference being deleted.
        /// @throws None (noexcept).
        void on_delete(const cdcl::clause::ref_t ref) noexcept
        {
            if (!ref.valid() || active_.erase(ref.offset()) == 0)
            {
                ++coverage_.structural_errors;
                return;
            }
            ++coverage_.clauses_deleted;
        }

        /// @brief Registers a shrink-clause proof event in checker state.
        /// @param ref Clause reference being shrunk.
        /// @throws None (noexcept).
        void on_shrink(const cdcl::clause::ref_t ref) noexcept
        {
            if (!ref.valid() || active_.find(ref.offset()) == active_.end())
            {
                ++coverage_.structural_errors;
                return;
            }
            ++coverage_.clauses_shrunk;
        }

        /// @brief Re-associates an active clause's tracked reference after physical relocation.
        /// @param old_ref Clause reference before relocation.
        /// @param new_ref Clause reference after relocation.
        /// @throws None (noexcept).
        void on_relocate(const cdcl::clause::ref_t old_ref, const cdcl::clause::ref_t new_ref) noexcept
        {
            if (!old_ref.valid() || !new_ref.valid() || old_ref.offset() == new_ref.offset())
            {
                return;
            }
            if (active_.erase(old_ref.offset()) != 0)
            {
                active_.insert(new_ref.offset());
            }
        }

        /// @brief Validates current checker state against expected proof conclusion conditions.
        /// @return True if no structural inconsistency was observed across the episode.
        /// @throws None (noexcept).
        [[nodiscard]] bool validate_conclusion() const noexcept { return coverage_.structural_errors == 0; }

        /// @brief Returns a snapshot of checker overhead/coverage counters.
        /// @return Coverage counters accumulated so far.
        /// @throws None (noexcept).
        [[nodiscard]] const coverage& coverage_snapshot() const noexcept { return coverage_; }

    private:
        std::unordered_set<cdcl::clause::ref_t::offset_t> active_ {};
        coverage coverage_ {};
    };
}
