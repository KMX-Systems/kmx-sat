/// @file inc/kmx/sat/proof/tracer/view.hpp
/// @brief Type-erased wrapper for proof sinks without virtual dispatch in the hot path, implemented as a closed
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Type-erased wrapper for proof sinks without virtual dispatch in the hot path, implemented as a closed
    /// std::variant over the concrete tracer types with std::visit-based tag dispatch; no vtable, no RTTI.
    ///
    /// `tracer::view` gives `proof::proof_manager` one uniform call surface (`add_original`/`add_derived`/
    /// `delete_clause`/`shrink_clause`/`finalize`) over whichever concrete tracer (`tracer::drat`, `tracer::lrat`,
    /// `tracer::frat`, `tracer::idrup`, `tracer::lidrup`, or `tracer::veripb`) is actually active, without
    /// introducing a class hierarchy or `virtual` dispatch: internally it forwards to `tracer::variant_t`
    /// (`std::variant` over the six concrete types) using `std::visit`-based tag dispatch. This matches the
    /// architecture's rule that type erasure is permitted only for tracers/cold sinks and must never use
    /// `dynamic_cast`/vtables/RTTI, keeping dispatch cost bounded and predictable even off the CDCL hot path.
    /// @note The call surface mirrored here is named structurally by the `tracer::like` concept, which every
    /// alternative of `tracer::variant_t` is statically asserted to satisfy.
    class view final
    {
    public:
        /// @brief Constructs an empty tracer view bound to no concrete tracer.
        /// @throws None (noexcept).
        view() noexcept = default;

        /// @brief Forwards an original-clause-added event to the active concrete tracer.
        /// @param ref Reference to the newly added original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Forwards a derived-clause-added event to the active concrete tracer.
        /// @param ref Reference to the newly derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Forwards a clause-deleted event to the active concrete tracer.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Forwards a clause-shrunk event to the active concrete tracer.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Forwards the proof-finalization event to the active concrete tracer.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
        }
    };
}
