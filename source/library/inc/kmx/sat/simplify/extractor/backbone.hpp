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

        void reset() noexcept
        {
            candidates_.clear();
            confirmed_.clear();
            emitted_.clear();
            std::fill(candidate_marks_.begin(), candidate_marks_.end(), 0u);
            std::fill(confirmed_marks_.begin(), confirmed_marks_.end(), 0u);
            std::fill(emitted_marks_.begin(), emitted_marks_.end(), 0u);
            unit_index_valid_ = false;
        }

        void record_candidate(const literal lit) noexcept
        {
            if (lit.raw() == 0u)
                return;
            if (marked(candidate_marks_, lit.negated()))
                return;
            if (!marked(candidate_marks_, lit))
            {
                mark(candidate_marks_, lit);
                candidates_.push_back(lit);
            }
        }

        void confirm_candidate(const literal lit) noexcept
        {
            if (!marked(candidate_marks_, lit))
                return;

            if (database_ != nullptr && has_unit_clause(lit.negated()))
                return;

            if (marked(confirmed_marks_, lit.negated()))
                return;

            if (!marked(confirmed_marks_, lit))
            {
                mark(confirmed_marks_, lit);
                confirmed_.push_back(lit);
            }
        }

        void emit_unit_fact(const literal lit) noexcept
        {
            if (!marked(confirmed_marks_, lit))
                return;

            if (marked(emitted_marks_, lit))
                return;

            if (database_ != nullptr && !has_unit_clause(lit))
            {
                const auto ref = database_->add_clause(std::array<literal, 1> {lit}, true);
                if (ref.valid())
                {
                    mark(unit_marks_, lit);
                    if (clause_sink_)
                        clause_sink_(ref);
                    if (proof_manager_ != nullptr)
                        proof_manager_->on_add_derived(ref, std::array<literal, 1> {lit});
                }
            }

            mark(emitted_marks_, lit);
            emitted_.push_back(lit);
        }

        std::size_t candidate_count() const noexcept { return candidates_.size(); }

        std::size_t confirmed_count() const noexcept { return confirmed_.size(); }

        std::size_t emitted_count() const noexcept { return emitted_.size(); }

        literal last_emitted_literal() const noexcept { return emitted_.empty() ? literal {} : emitted_.back(); }

    private:
        static bool marked(const std::vector<std::uint8_t>& marks, const literal lit) noexcept
        {
            const auto slot = static_cast<std::size_t>(lit.raw());
            return slot < marks.size() && marks[slot] != 0u;
        }

        static void mark(std::vector<std::uint8_t>& marks, const literal lit) noexcept
        {
            const auto slot = static_cast<std::size_t>(lit.raw());
            if (slot >= marks.size())
                marks.resize(slot + 1u, 0u);
            marks[slot] = 1u;
        }

        /// @brief Answers whether the database currently states `lit` as a unit clause, from an index built once
        /// per reset and kept current as units are emitted.
        bool has_unit_clause(const literal lit) noexcept
        {
            if (database_ == nullptr)
                return false;
            if (!unit_index_valid_)
            {
                std::fill(unit_marks_.begin(), unit_marks_.end(), 0u);
                const auto& storage = database_->storage_of();
                const auto index_unit = [&](const cdcl::clause::ref_t ref) noexcept
                {
                    const auto literals = storage.view_literals(ref);
                    if (literals.size() == 1u)
                        mark(unit_marks_, literals.front());
                };
                database_->iterate_irredundant(index_unit);
                database_->iterate_redundant(index_unit);
                unit_index_valid_ = true;
            }
            return marked(unit_marks_, lit);
        }

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
