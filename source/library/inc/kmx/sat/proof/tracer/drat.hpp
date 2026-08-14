/// @file inc/kmx/sat/proof/tracer/drat.hpp
/// @brief Concrete DRAT proof format tracer.
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
    /// @brief Concrete DRAT proof format tracer.
    /// @details
    /// DRAT (Deletion Resolution Asymmetric Tautology) is the de facto standard unsatisfiability proof format for SAT
    /// competitions: each derived clause must be RAT (resolution asymmetric tautology) with respect to the current
    /// clause set, which a DRAT checker can verify without needing explicit antecedent information. Per the proof
    /// format compatibility matrix, DRAT supports all resolution-based derivations (BVE, BCE, CCE, subsumption,
    /// vivification) but has no native representation for XOR/gate-level or cardinality reasoning, so
    /// `congruence_engine`/`gate_extractor` must emit resolution-equivalent events when this tracer is active.
    /// `add_original`/`add_derived` emit clause-addition lines, `delete_clause` emits deletion lines, `shrink_clause`
    /// emits the shrunk clause as a new derived clause, and `finalize` writes the proof's closing marker.
    /// @reference DRAT: "Efficient Certified RAT Verification" (Wetzler, Heule, Hunt) and the associated DRAT-trim
    /// checker used across SAT Competition tooling.
    class drat final
    {
    public:
        /// @brief Constructs a DRAT tracer with no buffered output.
        /// @throws None (noexcept).
        drat() noexcept = default;

        /// @brief Emits an original-clause line for the given clause.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::add_original, ref.offset()}); }

        /// @brief Emits a derived-clause (RAT) line for the given clause.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::add_derived, ref.offset()}); }

        /// @brief Emits a deletion line for the given clause.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::delete_clause, ref.offset()}); }

        /// @brief Emits the shrunk clause as a newly derived clause line.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::shrink_clause, ref.offset()}); }

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

        /// @brief Writes the proof's closing marker.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
            emitted_events_.push_back({event_kind::finalize, 0u});
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
            std::uint64_t ref_offset {};
        };

        std::size_t emitted_count() const noexcept { return emitted_events_.size(); }

        bool finalized() const noexcept { return finalized_; }

        const std::vector<emitted_event>& emitted_events() const noexcept { return emitted_events_; }

    private:
        std::vector<emitted_event> emitted_events_ {};
        bool finalized_ {};
    };
}
