/// @file inc/kmx/sat/simplify/engine/probing.hpp
/// @brief Root-level unit propagation with clause strengthening, feeding backbone candidates.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify::engine
{
    /// @brief Root-level unit propagation with clause strengthening, feeding backbone candidates.
    /// @details
    /// `run_failed_literal_probing` closes the formula under its unit clauses: every clause that becomes unit once
    /// the known units falsify all its other literals is shrunk to that literal in place, the literal joins the
    /// units, and the closure continues until no clause changes. Each derived literal is reported as a backbone
    /// candidate for `extractor::backbone` to confirm and emit.
    ///
    /// The closure runs in time linear in the formula: literals are propagated through occurrence lists and each
    /// clause carries a count of its still-unassigned literals, exactly as the search's own propagation would do
    /// it. The previous formulation rescanned every clause per derived unit through a hash map, which was
    /// quadratic and dominated the preprocessing of bounded-model-checking instances with thousands of units.
    /// A clause whose literals are all falsified is left alone: the search refutes the formula at its root.
    class probing final
    {
    public:
        probing() noexcept = default;

        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        void probe_literal(const literal lit) noexcept
        {
            (void)lit;
            ++probe_count_;
        }

        void run_failed_literal_probing() noexcept;

        void learn_hyper_binary() noexcept { ++hyper_binary_count_; }

        void record_backbone_candidate(const literal lit) noexcept;

        std::span<const literal> backbone_candidates() const noexcept { return backbone_candidates_; }

        std::size_t probe_count() const noexcept { return probe_count_; }

        std::size_t hyper_binary_count() const noexcept { return hyper_binary_count_; }

        std::size_t backbone_candidate_count() const noexcept { return backbone_candidate_count_; }

        bool probing_completed() const noexcept { return probing_completed_; }

    private:
        void propagate_units_to_fixpoint() noexcept;

        /// @brief Shrinks a clause with exactly one unassigned literal to that literal and propagates it.
        void strengthen_to_unit(const std::uint32_t index) noexcept;

        void assign_if_unassigned(const literal lit) noexcept;

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::size_t probe_count_ {};
        std::size_t hyper_binary_count_ {};
        std::size_t backbone_candidate_count_ {};
        std::vector<literal> backbone_candidates_ {};
        std::vector<std::uint8_t> candidate_marks_ {};
        std::vector<cdcl::clause::ref_t> refs_ {};
        std::vector<std::int8_t> values_ {};
        std::vector<literal> queue_ {};
        std::vector<std::uint32_t> occurrence_starts_ {};
        std::vector<std::uint32_t> occurrence_fill_ {};
        std::vector<std::uint32_t> occurrences_ {};
        std::vector<std::uint32_t> unassigned_counts_ {};
        std::vector<std::uint8_t> satisfied_ {};
        bool probing_completed_ {};
    };
}
