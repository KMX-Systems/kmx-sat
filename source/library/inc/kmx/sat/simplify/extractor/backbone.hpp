/// @file inc/kmx/sat/simplify/extractor/backbone.hpp
/// @brief Confirms backbone candidates produced by probing and emits them as unit facts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <functional>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/engine/probing.hpp>
#include <kmx/sat/simplify/engine/sweep.hpp>

namespace kmx::sat::simplify::extractor
{
    /// @brief Confirms backbone candidates produced by probing and emits them as unit facts.
    /// @details
    /// `record_candidate` collects literals that root-level propagation derived, `confirm_candidate` keeps those
    /// that no unit clause contradicts, and `emit_unit_fact` adds a unit clause for each confirmed literal the
    /// database does not already state. Membership in the three sets and the presence of unit clauses are
    /// answered from per-literal mark tables: the unit table is rebuilt once per `reset` from the database and
    /// kept current as units are emitted, so a run with thousands of candidates costs one pass over the
    /// formula rather than one per candidate.
    class backbone final
    {
    public:
        using clause_sink_t = std::function<void(cdcl::clause::ref_t)>;

        backbone() noexcept = default;

        void attach_database(cdcl::clause::database& database) noexcept
        {
            database_ = &database;
            unit_index_valid_ = false;
        }

        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        void attach_clause_sink(clause_sink_t sink) noexcept { clause_sink_ = std::move(sink); }

        void reset() noexcept;

        void record_candidate(const literal lit) noexcept;

        void confirm_candidate(const literal lit) noexcept;

        void emit_unit_fact(const literal lit) noexcept;

        std::size_t candidate_count() const noexcept { return candidates_.size(); }

        std::size_t confirmed_count() const noexcept { return confirmed_.size(); }

        std::size_t emitted_count() const noexcept { return emitted_.size(); }

        literal last_emitted_literal() const noexcept { return emitted_.empty() ? literal {} : emitted_.back(); }

    private:
        static bool marked(const std::vector<std::uint8_t>& marks, const literal lit) noexcept
        {
            const auto slot = static_cast<std::size_t>(lit.raw());
            return (slot < marks.size()) && (marks[slot] != 0u);
        }

        static void mark(std::vector<std::uint8_t>& marks, const literal lit) noexcept;

        /// @brief Answers whether the database currently states `lit` as a unit clause, from an index built once
        /// per reset and kept current as units are emitted.
        bool has_unit_clause(const literal lit) noexcept;

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        clause_sink_t clause_sink_ {};
        std::vector<literal> candidates_ {};
        std::vector<literal> confirmed_ {};
        std::vector<literal> emitted_ {};
        std::vector<std::uint8_t> candidate_marks_ {};
        std::vector<std::uint8_t> confirmed_marks_ {};
        std::vector<std::uint8_t> emitted_marks_ {};
        std::vector<std::uint8_t> unit_marks_ {};
        bool unit_index_valid_ {};
        engine::probing probing_ {};
        engine::sweep sweep_ {};
    };
}
