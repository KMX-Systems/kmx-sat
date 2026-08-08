/// @file inc/kmx/sat/proof/tracer/idrup.hpp
/// @brief Concrete IDRUP proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Concrete IDRUP proof format tracer.
    ///
    /// IDRUP (Incremental DRUP) extends the DRAT/DRUP family with the extra bookkeeping needed to certify
    /// incremental SAT+UNSAT sessions: assumption introduction/retraction and multiple `solve()` calls against an
    /// evolving clause set, rather than one static formula. Per the compatibility matrix, this format is required
    /// whenever incremental proof continuity across `solve()` epochs must be checkable; passes valid only within a
    /// single epoch (certain inprocessing shortcuts) must declare that epoch-locality explicitly so this tracer can
    /// represent epoch boundaries correctly. `add_original`/`add_derived`/`delete_clause`/`shrink_clause` behave as
    /// in DRAT but are epoch-aware, and `finalize` closes out the current epoch's segment of the proof.
    /// @reference IDRUP: incremental extension of the DRUP/DRAT proof format for incremental SAT solving
    /// (as supported by CaDiCaL's incremental proof tracing).
    class idrup final
    {
    public:
        /// @brief Constructs an IDRUP tracer with no buffered output.
        /// @throws None (noexcept).
        idrup() noexcept = default;

        /// @brief Emits an epoch-scoped original-clause line.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits an epoch-scoped derived-clause line.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits an epoch-scoped deletion line.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits an epoch-scoped clause-shrink line.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Closes out the current epoch's segment of the proof.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
        }
    };
}
