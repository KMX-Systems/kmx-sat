/// @file inc/kmx/sat/proof/tracer/idrup.hpp
/// @brief Concrete IDRUP proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/proof/event_stream.hpp>

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
            emitted_events_.push_back({event_kind::add_original, current_epoch_, ref.offset()});
        }

        /// @brief Emits an epoch-scoped derived-clause line.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
            emitted_events_.push_back({event_kind::add_derived, current_epoch_, ref.offset()});
        }

        /// @brief Emits an epoch-scoped deletion line.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
            emitted_events_.push_back({event_kind::delete_clause, current_epoch_, ref.offset()});
        }

        /// @brief Emits an epoch-scoped clause-shrink line.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
            emitted_events_.push_back({event_kind::shrink_clause, current_epoch_, ref.offset()});
        }

        /// @brief Emits one tracer record from a fully-populated proof event.
        /// @param event Proof event payload.
        /// @throws None (noexcept).
        void on_event(const proof::proof_event& event) noexcept
        {
            switch (event.kind)
            {
                case proof::event_kind::add_original:
                    add_original(event.clause_ref);
                    break;
                case proof::event_kind::add_derived:
                    add_derived(event.clause_ref);
                    break;
                case proof::event_kind::delete_clause:
                    delete_clause(event.clause_ref);
                    break;
                case proof::event_kind::shrink_clause:
                    shrink_clause(event.clause_ref);
                    break;
                case proof::event_kind::conclusion:
                    finalize();
                    break;
            }
        }

        /// @brief Closes out the current epoch's segment of the proof.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
            emitted_events_.push_back({event_kind::finalize, current_epoch_, 0u});
            ++current_epoch_;
            finalized_ = true;
        }

        enum class event_kind
        {
            add_original,
            add_derived,
            delete_clause,
            shrink_clause,
            finalize,
        };

        struct emitted_event
        {
            event_kind kind {};
            std::uint32_t epoch {};
            std::uint64_t ref_offset {};
        };

        std::size_t emitted_count() const noexcept { return emitted_events_.size(); }

        bool finalized() const noexcept { return finalized_; }

        std::uint32_t current_epoch() const noexcept { return current_epoch_; }

        const std::vector<emitted_event>& emitted_events() const noexcept { return emitted_events_; }

    private:
        std::vector<emitted_event> emitted_events_ {};
        std::uint32_t current_epoch_ {};
        bool finalized_ {};
    };
}
