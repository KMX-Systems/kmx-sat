/// @file inc/kmx/sat/simplify/extractor/backbone.hpp
/// @brief Binary or sweep-discovered backbone extraction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
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
    /// @brief Binary or sweep-discovered backbone extraction.
    /// @details
    /// A backbone literal is true in every model of the formula; knowing one lets the solver fix it as a permanent
    /// unit fact instead of re-deriving it repeatedly. `extractor::backbone` consolidates candidates from two
    /// sources: `engine::probing::record_backbone_candidate` (a literal implied identically under both polarities
    /// during failed-literal probing) and `engine::sweep::extract_backbone` (confirmed exhaustively by the embedded
    /// micro-solver over a small variable cluster). `record_candidate` stages a candidate from either source;
    /// `confirm_candidate` re-validates it against the full clause set before committing; `emit_unit_fact` registers
    /// the confirmed backbone literal as a permanent unit clause, reporting it to `proof::proof_manager` like any
    /// other derived fact.
    class backbone final
    {
    public:
        using clause_sink_t = std::function<void(cdcl::clause::ref_t)>;

        /// @brief Constructs a backbone extractor with embedded probing and sweep engines.
        /// @throws None (noexcept).
        backbone() noexcept = default;

        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        void attach_clause_sink(clause_sink_t sink) noexcept { clause_sink_ = std::move(sink); }

        /// @brief Clears episode-local candidates, confirmations, and emitted-fact history.
        void reset() noexcept
        {
            candidates_.clear();
            confirmed_.clear();
            emitted_.clear();
        }

        /// @brief Stages a candidate backbone literal discovered by probing or sweeping.
        /// @param lit Candidate literal.
        /// @throws None (noexcept).
        void record_candidate(const literal lit) noexcept
        {
            if (lit.raw() == 0u)
                return;
            if (std::find(candidates_.begin(), candidates_.end(), lit.negated()) != candidates_.end())
                return;
            if (std::find(candidates_.begin(), candidates_.end(), lit) == candidates_.end())
                candidates_.push_back(lit);
        }

        /// @brief Re-validates a staged candidate against the full clause set before committing it.
        /// @param lit Candidate literal to confirm.
        /// @throws None (noexcept).
        void confirm_candidate(const literal lit) noexcept
        {
            if (std::find(candidates_.begin(), candidates_.end(), lit) == candidates_.end())
                return;

            if (database_ != nullptr && has_unit_clause(lit.negated()))
                return;

            if (std::find(confirmed_.begin(), confirmed_.end(), lit.negated()) != confirmed_.end())
                return;

            if (std::find(confirmed_.begin(), confirmed_.end(), lit) == confirmed_.end())
                confirmed_.push_back(lit);
        }

        /// @brief Registers a confirmed backbone literal as a permanent unit fact.
        /// @param lit Confirmed backbone literal.
        /// @throws None (noexcept).
        void emit_unit_fact(const literal lit) noexcept
        {
            if (std::find(confirmed_.begin(), confirmed_.end(), lit) == confirmed_.end())
                return;

            if (std::find(emitted_.begin(), emitted_.end(), lit) != emitted_.end())
                return;

            if (database_ != nullptr && !has_unit_clause(lit))
            {
                const auto ref = database_->add_clause(std::array<literal, 1> {lit}, true);
                if (ref.valid() && clause_sink_)
                    clause_sink_(ref);
                if (ref.valid() && proof_manager_ != nullptr)
                    proof_manager_->on_add_derived(ref, std::array<literal, 1> {lit});
            }

            emitted_.push_back(lit);
        }

        std::size_t candidate_count() const noexcept { return candidates_.size(); }

        std::size_t confirmed_count() const noexcept { return confirmed_.size(); }

        std::size_t emitted_count() const noexcept { return emitted_.size(); }

        literal last_emitted_literal() const noexcept { return emitted_.empty() ? literal {} : emitted_.back(); }

    private:
        bool has_unit_clause(const literal lit) const noexcept
        {
            if (database_ == nullptr)
                return false;

            bool found = false;
            const auto& storage = database_->storage_of();
            const auto search_ref = [&found, &storage, lit](const cdcl::clause::ref_t ref) noexcept
            {
                const auto literals = storage.view_literals(ref);
                 if (literals.size() == 1u && literals.front() == lit)
                        found = true;
            };

            database_->iterate_irredundant(search_ref);
            if (found)
                return true;

            database_->iterate_redundant(search_ref);
            return found;
        }

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        clause_sink_t clause_sink_ {};
        std::vector<literal> candidates_ {};
        std::vector<literal> confirmed_ {};
        std::vector<literal> emitted_ {};
        engine::probing probing_ {};
        engine::sweep sweep_ {};
    };
}
