/// @file api/kmx/sat/proof_manager.hpp
/// @brief Single source of proof events. Public at the activation interface level only; concrete tracer
/// implementations remain internal.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/checker/lrat.hpp>
#include <kmx/sat/proof/checker/online.hpp>
#include <kmx/sat/proof/clause/id_allocator.hpp>
#include <kmx/sat/proof/event_stream.hpp>
#include <kmx/sat/proof/format.hpp>
#include <kmx/sat/proof/tracer/view.hpp>

namespace kmx::sat
{
    /// @brief Single source of proof events. Public at the activation interface level only; concrete tracer
    /// implementations remain internal.
    /// @details
    /// `proof_manager` is the central broker between CDCL lifecycle events and proof sinks. It tracks which proof
    /// formats are active, assigns stable clause identities via `proof::clause::id_allocator`, and forwards each
    /// structural event (`add_original`, `add_derived`, `delete_clause`, `shrink_clause`, and final conclusion)
    /// through `proof::event_stream` and registered tracer views. Keeping this fan-out in one place ensures
    /// consistency between buffered proof output and synchronous tracer callbacks.
    class proof_manager final
    {
    public:
        /// @brief Constructs a proof manager with no formats enabled.
        /// @throws None (noexcept).
        proof_manager() noexcept = default;

        /// @brief Enables one proof format's tracer.
        /// @param id Proof format to enable.
        /// @throws None (noexcept).
        void enable_format(const proof::format_id id) noexcept;

        /// @brief Disables every currently enabled proof format.
        /// @throws None (noexcept).
        void disable_all() noexcept
        {
            enabled_format_mask_ = 0u;
            enabled_tracers_.clear();
        }

        /// @brief Returns whether any proof format is currently enabled.
        /// @return True if at least one proof format is active.
        [[nodiscard]] bool has_enabled_formats() const noexcept { return enabled_format_mask_ != 0u; }

        /// @brief Returns whether anything would observe an emitted proof event.
        /// @details Lets a caller skip assembling event payloads -- notably per-conflict antecedent chains, which
        /// cost a stable-id lookup and a duplicate scan per resolution step -- when no tracer, checker or event
        /// buffer is attached to read them. A solve with proofs disabled is the default configuration, so this is
        /// the common case rather than an optimization for an unusual one.
        [[nodiscard]] bool has_active_consumers() const noexcept
        {
            return event_buffering_enabled_ || online_checker_enabled_ || lrat_checker_enabled_ || !enabled_tracers_.empty() ||
                   !registered_tracers_.empty();
        }

        /// @brief Returns whether a specific proof format is currently enabled.
        /// @param id Proof format to query.
        /// @return True if @p id is active.
        [[nodiscard]] bool has_enabled_format(const proof::format_id id) const noexcept
        {
            return (enabled_format_mask_ & format_bit(id)) != 0u;
        }

        /// @brief Registers an external sink that receives buffered proof events from the event stream.
        /// @param sink Callback invoked for every buffered event during flush.
        /// @throws None (noexcept).
        void set_event_sink(proof::event_stream::sink_t sink) noexcept
        {
            event_buffering_enabled_ = true;
            event_stream_.set_sink(std::move(sink));
        }

        /// @brief Enables or disables retaining dispatched events in the event stream.
        /// @details Callers that never read the buffer (a solve with no proof output configured) can disable it to
        /// avoid building and later freeing one buffered event per clause action. Attaching any tracer, checker or
        /// sink re-enables buffering.
        /// @throws None (noexcept).
        void set_event_buffering(const bool enabled) noexcept { event_buffering_enabled_ = enabled; }

        /// @brief Enables an internal proof checker to run alongside the enabled tracers.
        /// @param id Internal checker to enable.
        /// @throws None (noexcept).
        void enable_checker(const proof::checker_id id) noexcept;

        /// @brief Registers an external tracer sink to receive proof events alongside internally enabled formats.
        /// @param sink Tracer sink to register.
        /// @throws None (noexcept).
        void register_tracer(const proof::tracer::view& sink) noexcept;

        /// @brief Reports that an original (non-redundant) problem clause was added.
        /// @param ref Reference to the newly added clause.
        /// @param literals Clause's literal content, recorded verbatim for tracers/checkers.
        /// @throws None (noexcept).
        void on_add_original(const cdcl::clause::ref_t ref, const std::span<const literal> literals = {}) noexcept
        {
            dispatch_event(proof::event_kind::add_original, ref, literals);
        }

        /// @brief Reports that a derived (learned) clause was added.
        /// @param ref Reference to the newly derived clause.
        /// @param literals Clause's literal content, recorded verbatim for tracers/checkers.
        /// @param antecedents Ordered antecedent clause ids justifying the derivation, if already known.
        /// @throws None (noexcept).
        void on_add_derived(const cdcl::clause::ref_t ref, const std::span<const literal> literals = {},
                            const std::span<const proof::clause::id> antecedents = {}) noexcept;

        /// @brief Gives a clause that predates the first consumer a proof id, so later deletions and antecedent
        /// references can name it; a clause that already has an id keeps it.
        void adopt_clause(const cdcl::clause::ref_t ref) noexcept { (void)id_allocator_.allocate_for_new_clause(ref); }

        /// @brief Reports that a clause was deleted.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void on_delete_clause(const cdcl::clause::ref_t ref) noexcept { dispatch_event(proof::event_kind::delete_clause, ref); }

        /// @brief Reports that a clause was shrunk in place.
        /// @param ref Reference to the shrunk clause.
        /// @param literals Clause's literal content after shrinking.
        /// @throws None (noexcept).
        void on_shrink_clause(const cdcl::clause::ref_t ref, const std::span<const literal> literals = {}) noexcept
        {
            dispatch_event(proof::event_kind::shrink_clause, ref, literals);
        }

        /// @brief Reports that a clause was physically relocated and keeps its logical proof id stable.
        /// @param old_ref Clause reference before relocation.
        /// @param new_ref Clause reference after relocation.
        /// @throws None (noexcept).
        void on_clause_relocated(const cdcl::clause::ref_t old_ref, const cdcl::clause::ref_t new_ref) noexcept;

        /// @brief Finalizes the proof with the episode's SAT/UNSAT conclusion.
        /// @throws None (noexcept).
        void on_conclusion() noexcept { dispatch_event(proof::event_kind::conclusion, {}); }

        /// @brief Drains any buffered proof events to their configured output sinks.
        /// @throws None (noexcept).
        void flush() noexcept { event_stream_.flush_sync(); }

        /// @brief Returns the currently active proof id mapped to a clause reference.
        /// @param ref Clause reference to query.
        /// @return Active proof id for `ref`, or invalid if none exists.
        /// @throws None (noexcept).
        [[nodiscard]] proof::clause::id stable_id_for_clause(const cdcl::clause::ref_t ref) const noexcept
        {
            return id_allocator_.id_for_clause_ref(ref);
        }

        /// @brief Returns the most recently dispatched proof event.
        /// @return Last event emitted via `dispatch_event` or `on_conclusion`.
        /// @throws None (noexcept).
        [[nodiscard]] const proof::proof_event& last_event() const noexcept { return last_event_; }

        /// @brief Returns the number of proof events currently buffered in the stream.
        /// @return Buffered event count.
        /// @throws None (noexcept).
        [[nodiscard]] std::size_t buffered_event_count() const noexcept { return event_stream_.buffered_count(); }

        /// @brief Returns a read-only view of currently buffered proof events.
        /// @return Span over buffered proof events.
        [[nodiscard]] std::span<const proof::proof_event> buffered_events() const noexcept { return event_stream_.buffered_events(); }

        /// @brief Records a derived/original clause's literal content for the internal LRAT checker.
        /// @param clause_id Stable proof identity of the clause (typically from `stable_id_for_clause`).
        /// @param literals Literal snapshot to associate with `clause_id`.
        /// @throws None (noexcept).
        void record_checker_clause(const proof::clause::id clause_id, const std::span<const literal> literals) noexcept
        {
            if (lrat_checker_enabled_)
                lrat_checker_.record_clause(clause_id, literals);
        }

        /// @brief Records the ordered antecedent chain justifying a derived clause for the internal LRAT checker.
        /// @param clause_id Stable proof identity of the derived clause.
        /// @param antecedents Ordered antecedent clause ids to replay during chain verification.
        /// @throws None (noexcept).
        void record_checker_antecedents(const proof::clause::id clause_id, const std::span<const proof::clause::id> antecedents) noexcept
        {
            if (lrat_checker_enabled_)
                lrat_checker_.record_antecedents(clause_id, antecedents);
        }

        /// @brief Returns the online checker's coverage/overhead counters.
        /// @return Coverage counters accumulated by the internal online checker.
        /// @throws None (noexcept).
        [[nodiscard]] const proof::checker::online::coverage& checker_coverage() const noexcept
        {
            return online_checker_.coverage_snapshot();
        }

        /// @brief Validates every internal checker enabled for this episode.
        /// @return True if the online checker (when enabled) saw no structural error and the LRAT checker (when
        /// enabled and fed at least one antecedent chain) confirms every recorded chain resolves to its clause.
        /// @throws None (noexcept).
        [[nodiscard]] bool validate_checkers() const noexcept;

    private:
        /// @brief Returns the enabled-format mask bit representing one proof format.
        static constexpr std::uint32_t format_bit(const proof::format_id id) noexcept
        {
            return std::uint32_t {1u} << static_cast<std::uint8_t>(id);
        }

        proof::clause::id resolve_clause_id(const proof::event_kind kind, const cdcl::clause::ref_t ref) noexcept;

        static std::int32_t to_dimacs_int(const literal lit) noexcept
        {
            const auto index = static_cast<std::int32_t>(lit.variable_of().index());
            return lit.is_negated() ? -index : index;
        }

        void dispatch_event(const proof::event_kind kind, const cdcl::clause::ref_t ref, const std::span<const literal> literals = {},
                            const std::span<const proof::clause::id> antecedents = {}) noexcept;

        proof::clause::id_allocator id_allocator_ {};
        proof::event_stream event_stream_ {};
        std::uint32_t enabled_format_mask_ {};
        std::vector<proof::tracer::view> enabled_tracers_ {};
        std::vector<proof::tracer::view> registered_tracers_ {};
        proof::proof_event last_event_ {};
        proof::checker::online online_checker_ {};
        proof::checker::lrat lrat_checker_ {};
        bool online_checker_enabled_ {};
        bool lrat_checker_enabled_ {};
        bool event_buffering_enabled_ {true};
    };
}
