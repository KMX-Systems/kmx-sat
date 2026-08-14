/// @file inc/kmx/sat/proof/tracer/lrat.hpp
/// @brief Concrete LRAT proof format tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/proof/event_stream.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Concrete LRAT proof format tracer.
    /// @details
    /// LRAT (Linear RAT) extends DRAT with explicit antecedent clause-id chains for every derivation, letting a
    /// checker verify each step in linear time instead of DRAT's more expensive RAT search. Per the compatibility
    /// matrix, LRAT carries the same resolution-only representational constraint as DRAT, with the added requirement
    /// that `proof::clause::id_allocator` guarantee stable clause ids across every pass active while this tracer is
    /// enabled, since a stale id would break the antecedent chain. `add_original`/`add_derived` emit clause lines
    /// together with their resolved antecedent id sequence (built by `conflict_analyzer::build_resolution_chain`),
    /// `delete_clause`/`shrink_clause` emit the corresponding id-referencing events, and `finalize` closes the proof.
    /// @reference LRAT: "Extended Resolution Simulates DRAT" / "The LRAT proof format" (Cruz-Filipe et al.), building
    /// on DRAT with explicit antecedent chains for linear-time checking.
    class lrat final
    {
    public:
        /// @brief Constructs an LRAT tracer with no buffered output.
        /// @throws None (noexcept).
        lrat() noexcept = default;

        /// @brief Emits an original-clause line with its assigned clause id.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::add_original, ref.offset()}); }

        /// @brief Emits a derived-clause line together with its resolved antecedent id chain.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::add_derived, ref.offset()}); }

        /// @brief Emits a deletion line referencing the clause's stable id.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::delete_clause, ref.offset()}); }

        /// @brief Emits the shrunk clause as a newly derived clause line with its antecedent chain.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::shrink_clause, ref.offset()}); }

        /// @brief Emits one tracer record from a fully-populated proof event.
        /// @param event Proof event payload.
        /// @throws None (noexcept).
        void on_event(const proof::proof_event& event) noexcept
        {
            event_kind kind {};
            switch (event.kind)
            {
                case proof::event_kind::add_original:
                    kind = event_kind::add_original;
                    break;
                case proof::event_kind::add_derived:
                    kind = event_kind::add_derived;
                    break;
                case proof::event_kind::delete_clause:
                    kind = event_kind::delete_clause;
                    break;
                case proof::event_kind::shrink_clause:
                    kind = event_kind::shrink_clause;
                    break;
                case proof::event_kind::conclusion:
                    finalize();
                    return;
            }

            emitted_event record {};
            record.kind = kind;
            record.ref_offset = event.clause_ref.offset();
            record.clause_id_value = event.clause_id.value();
            record.literals = event.literals;
            record.antecedent_id_values.reserve(event.antecedent_ids.size());
            std::transform(event.antecedent_ids.begin(), event.antecedent_ids.end(), std::back_inserter(record.antecedent_id_values),
                           [](const auto antecedent) { return antecedent.value(); });
            emitted_events_.push_back(record);
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
            std::uint64_t clause_id_value {};
            std::vector<int32_t> literals {};
            std::vector<std::uint64_t> antecedent_id_values {};
        };

        std::size_t emitted_count() const noexcept { return emitted_events_.size(); }

        bool finalized() const noexcept { return finalized_; }

        const std::vector<emitted_event>& emitted_events() const noexcept { return emitted_events_; }

    private:
        std::vector<emitted_event> emitted_events_ {};
        bool finalized_ {};
    };
}
