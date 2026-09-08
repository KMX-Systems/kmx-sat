/// @file inc/kmx/sat/proof/tracer/frat.hpp
/// @brief Concrete FRAT proof format tracer.
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
    /// @brief Concrete FRAT proof format tracer.
    /// @details
    /// FRAT (Flexible RAT) records the same resolution-only derivations as DRAT/LRAT but as structured,
    /// self-describing records (with optional antecedent information) rather than DRAT's terse line format, making it
    /// cheaper to emit on the hot path while still supporting out-of-band checking, at the cost of a larger proof
    /// file than LRAT's compact form. Per the compatibility matrix it shares LRAT's requirement that
    /// `proof::clause::id_allocator` keep clause ids stable across every active pass. `add_original`/`add_derived`
    /// emit structured addition records, `delete_clause`/`shrink_clause` emit structured removal/shrink records, and
    /// `finalize` closes the proof.
    /// @reference FRAT: "FRAT: A Flexible, Efficient Certified Deduction Format" (Baek, Carneiro, Heule).
    class frat final
    {
    public:
        /// @brief Constructs an FRAT tracer with no buffered output.
        /// @throws None (noexcept).
        frat() noexcept = default;

        /// @brief Emits a structured original-clause addition record.
        /// @param ref Reference to the original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::add_original, ref.offset()}); }

        /// @brief Emits a structured derived-clause addition record.
        /// @param ref Reference to the derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::add_derived, ref.offset()}); }

        /// @brief Emits a structured clause-deletion record.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::delete_clause, ref.offset()}); }

        /// @brief Emits a structured clause-shrink record.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept { emitted_events_.push_back({event_kind::shrink_clause, ref.offset()}); }

        /// @brief Emits one tracer record from a fully-populated proof event.
        /// @param event Proof event payload.
        /// @throws None (noexcept).
        void on_event(const proof::proof_event& event) noexcept;

        /// @brief Writes the proof's closing marker.
        /// @throws None (noexcept).
        void finalize() noexcept;

        /// @brief Clears emitted FRAT records and reopens the tracer for a fresh proof episode.
        void reset() noexcept
        {
            emitted_events_.clear();
            finalized_ = false;
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
