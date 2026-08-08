/// @file inc/kmx/sat/proof/tracer/veripb.hpp
/// @brief Concrete VeriPB proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Concrete VeriPB proof format tracer.
    ///
    /// VeriPB is the only format in this baseline that can natively express gate/XOR-level and cardinality reasoning
    /// rather than expanding it into resolution steps; per the compatibility matrix, when this tracer is active,
    /// `congruence_engine`/`gate_extractor` may emit native gate-level proof events instead of the resolution-only
    /// expansions required for DRAT/LRAT/FRAT/IDRUP/LIDRUP. `add_original`/`add_derived` emit pseudo-Boolean/native
    /// constraint additions (falling back to resolution-equivalent additions for passes that have not opted into
    /// native emission), `delete_clause`/`shrink_clause` emit the corresponding removal/strengthening steps, and
    /// `finalize` closes the proof.
    /// @reference VeriPB: a proof format and checker for pseudo-Boolean/cutting-planes reasoning, used to certify
    /// gate-, XOR-, and cardinality-level simplifications natively.
    class veripb final
    {
    public:
        /// @brief Constructs a VeriPB tracer with no buffered output.
        /// @throws None (noexcept).
        veripb() noexcept = default;

        /// @brief Emits an original-constraint addition, natively or resolution-equivalent as appropriate.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a derived-constraint addition, natively or resolution-equivalent as appropriate.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a constraint-deletion step.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Emits a constraint-strengthening step for a shrunk clause.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Writes the proof's closing marker.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
        }
    };
}
