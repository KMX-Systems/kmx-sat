/// @file api/kmx/sat/proof_manager.hpp
/// @brief Single source of proof events. Public at the activation interface level only; concrete tracer
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <string_view>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/proof/clause/id_allocator.hpp>
#include <kmx/sat/proof/event_stream.hpp>
#include <kmx/sat/proof/tracer/view.hpp>

namespace kmx::sat
{
    /// @brief Single source of proof events. Public at the activation interface level only; concrete tracer
    /// implementations remain internal.
    ///
    /// `proof_manager` is the only component `conflict_analyzer`, `clause::learner`, `otf_strengthener`, and every
    /// simplification pass call into when they add, derive, delete, or shrink a clause; it then fans those events
    /// out to whichever concrete tracers (`tracer::drat`/`lrat`/`frat`/`idrup`/`lidrup`/`veripb`, reached only
    /// through the type-erased `tracer::view`) are currently enabled via `enable_format`/`disable_all`.
    /// `register_tracer` attaches an external sink (for example `solver::attach_proof_sink`); `on_add_original`/
    /// `on_add_derived`/`on_delete_clause`/`on_shrink_clause` correspond to the four structural events every clause
    /// lifecycle transition must report; `on_conclusion` finalizes the SAT/UNSAT verdict for proof-checking purposes;
    /// `flush` drains buffered events through `event_stream`/`io::proof_output_pipeline`.
    /// @note Only this class's activation interface (enabling formats, attaching sinks, the four structural events)
    /// is part of the stable public surface; the concrete tracer implementations behind it are internal and may
    /// change without a major version bump, per the public-interface stability policy.
    /// @warning Per the format-compatibility enforcement rule, every simplification pass whose proof representation
    /// is format-sensitive (BCE/CCE, BVE, congruence/gate reasoning) must query this manager for the currently
    /// enabled format(s) before running, and fall back to a resolution-only strategy or skip the pass when no
    /// compatible representation is available.
    class proof_manager final
    {
    public:
        /// @brief Constructs a proof manager with no formats enabled.
        /// @throws None (noexcept).
        proof_manager() noexcept = default;

        /// @brief Enables a named proof format's tracer.
        /// @param format_name Identifier of the proof format to enable (for example "drat", "lrat", "frat").
        /// @throws None (noexcept).
        void enable_format(const std::string_view format_name) noexcept
        {
        }

        /// @brief Disables every currently enabled proof format.
        /// @throws None (noexcept).
        void disable_all() noexcept
        {
        }

        /// @brief Registers an external tracer sink to receive proof events alongside internally enabled formats.
        /// @param sink Tracer sink to register.
        /// @throws None (noexcept).
        void register_tracer(const tracer::view& sink) noexcept
        {
        }

        /// @brief Reports that an original (non-redundant) problem clause was added.
        /// @param ref Reference to the newly added clause.
        /// @throws None (noexcept).
        void on_add_original(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Reports that a derived (learned) clause was added.
        /// @param ref Reference to the newly derived clause.
        /// @throws None (noexcept).
        void on_add_derived(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Reports that a clause was deleted.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void on_delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Reports that a clause was shrunk in place.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void on_shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Finalizes the proof with the episode's SAT/UNSAT conclusion.
        /// @throws None (noexcept).
        void on_conclusion() noexcept
        {
        }

        /// @brief Drains any buffered proof events to their configured output sinks.
        /// @throws None (noexcept).
        void flush() noexcept
        {
        }

    private:
        clause::id_allocator id_allocator_ {};
        event_stream event_stream_ {};
    };
}
