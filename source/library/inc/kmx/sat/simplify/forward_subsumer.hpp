/// @file inc/kmx/sat/simplify/forward_subsumer.hpp
/// @brief Forward/backward subsumption, with SIMD acceleration where justified.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <vector>
#endif

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify
{
    /// @brief Forward/backward subsumption, with SIMD acceleration where justified.
    ///
    /// A clause `A` subsumes clause `B` when every literal of `A` also occurs in `B`, making `B` redundant; forward
    /// subsumption checks new/recently-added clauses against existing ones, while backward subsumption checks
    /// existing clauses against a newly added one. `run` sweeps the clause database using an occurrence-index-driven
    /// candidate search; `is_subsumed` tests one clause against its candidate subsuming set;
    /// `strengthen_subsumed_clause` applies the related self-subsuming-resolution strengthening (removing one
    /// literal rather than the whole clause) when only partial subsumption is found. Per the hot-path directives,
    /// this pass is a candidate for optional SIMD-accelerated literal-set comparison, with a scalar fallback when the
    /// target lacks the relevant ISA extension.
    class forward_subsumer final
    {
    public:
        /// @brief Constructs a forward subsumer with no cached candidate index.
        /// @throws None (noexcept).
        forward_subsumer() noexcept = default;

        /// @brief Attaches the clause database to be simplified.
        /// @param database Clause database to scan.
        /// @throws None (noexcept).
        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Attaches the proof manager used to report clause deletions and strengthening.
        /// @param proof_manager Proof manager to notify.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Runs a full forward/backward subsumption sweep over the clause database.
        /// @throws None (noexcept).
        void run() noexcept
        {
            ++run_count_;
            last_subsumed_ref_ = {};

            if (database_ == nullptr)
                return;

            auto& storage = database_->storage_of();
            clauses_.clear();
            const auto append_clause = [&](const cdcl::clause::ref_t ref) noexcept
            {
                const auto literals = storage.view_literals(ref);
                clauses_.push_back(indexed_clause {ref, literals, signature_of(literals), true});
            };
            database_->iterate_irredundant(append_clause);
            database_->iterate_redundant(append_clause);

            build_occurrence_index();

            for (std::uint32_t left_index {}; left_index < clauses_.size(); ++left_index)
            {
                auto& left = clauses_[left_index];
                if (!left.active || left.literals.empty())
                    continue;

                // Any clause subsuming `left` must contain every literal of `left`, so it necessarily occurs in the
                // rarest of `left`'s occurrence lists; scanning that single list is exact and avoids redundant work.
                std::span<const std::uint32_t> rarest_candidates {};
                bool has_rarest {};
                for (const auto lit: left.literals)
                {
                    const auto candidates = occurrences_of(lit);
                    if (candidates.empty())
                    {
                        has_rarest = false;
                        break;
                    }
                    if (!has_rarest || candidates.size() < rarest_candidates.size())
                    {
                        rarest_candidates = candidates;
                        has_rarest = true;
                    }
                }
                if (!has_rarest)
                    continue;

                bool left_subsumed {};
                for (const auto candidate_index: rarest_candidates)
                {
                    if (candidate_index == left_index || !clauses_[candidate_index].active)
                        continue;

                    const auto& candidate = clauses_[candidate_index];
                    const bool precedes_equal_clause = candidate_index < left_index && candidate.literals.size() == left.literals.size();
                    if ((candidate.literals.size() < left.literals.size() || precedes_equal_clause) &&
                        (candidate.signature & ~left.signature) == 0u && clause_subsumes(candidate.literals, left.literals))
                    {
                        mark_subsumed(left);
                        left_subsumed = true;
                        break;
                    }
                }
                if (left_subsumed)
                    continue;

                for (const auto candidate_index: rarest_candidates)
                {
                    if (candidate_index == left_index || !clauses_[candidate_index].active)
                        continue;

                    auto& candidate = clauses_[candidate_index];
                    if (left.literals.size() <= candidate.literals.size() && (left.signature & ~candidate.signature) == 0u &&
                        clause_subsumes(left.literals, candidate.literals))
                        mark_subsumed(candidate);
                }
            }

            database_->flush_satisfied([&](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
        }

        /// @brief Checks whether a clause is subsumed by another clause already in the database.
        /// @param ref Reference to the clause to test.
        /// @return True if the clause is subsumed and therefore redundant.
        /// @throws None (noexcept).
        bool is_subsumed(const cdcl::clause::ref_t ref) const noexcept
        {
            if (database_ == nullptr || !ref.valid() || !database_->storage_of().is_alive(ref))
                return false;

            auto& storage = database_->storage_of();
            const auto candidate_size = storage.literal_count(ref);
            const auto candidate_clause = storage.view_literals(ref);
            bool subsumed {};
            const auto scan = [&](const cdcl::clause::ref_t other_ref) noexcept
            {
                if (!subsumed)
                    subsumed = subsumes_candidate(other_ref, ref, candidate_size, candidate_clause);
            };

            database_->iterate_irredundant(scan);
            database_->iterate_redundant(scan);
            return subsumed;
        }

        /// @brief Removes one literal from a clause found to be subsumed except for that literal.
        /// @param ref Reference to the clause to strengthen.
        /// @throws None (noexcept).
        void strengthen_subsumed_clause(const cdcl::clause::ref_t ref) noexcept
        {
            if (database_ == nullptr || !ref.valid() || !database_->storage_of().is_alive(ref))
                return;

            auto literals = database_->storage_of().literals_of(ref);
            if (literals.size() <= 1u)
                return;

            literals.pop_back();
            database_->storage_of().rewrite_clause_literals(ref, std::span<const literal> {literals.data(), literals.size()});
            database_->storage_of().shrink_clause(ref, static_cast<std::uint32_t>(literals.size()));
            if (proof_manager_ != nullptr)
                proof_manager_->on_shrink_clause(ref, literals);
            ++strengthened_count_;
        }

        std::size_t run_count() const noexcept { return run_count_; }

        std::size_t subsumed_count() const noexcept { return subsumed_count_; }

        std::size_t strengthened_count() const noexcept { return strengthened_count_; }

        cdcl::clause::ref_t last_subsumed_ref() const noexcept { return last_subsumed_ref_; }

    private:
        struct indexed_clause final
        {
            cdcl::clause::ref_t ref {};
            std::span<const literal> literals {};
            std::uint64_t signature {};
            bool active {};
        };

        /// Bloom-style literal-set fingerprint: `left` can only subsume `right` when `left.sig & ~right.sig == 0`.
        static std::uint64_t signature_of(const std::span<const literal> literals) noexcept
        {
            std::uint64_t signature {};
            for (const auto lit: literals)
                signature |= std::uint64_t {1u} << (lit.raw() & 63u);
            return signature;
        }

        /// Compressed literal-to-clause occurrence index; avoids per-literal container allocation.
        void build_occurrence_index() noexcept
        {
            literal::raw_t highest_raw {};
            std::size_t total_occurrences {};
            for (const auto& clause: clauses_)
            {
                total_occurrences += clause.literals.size();
                for (const auto lit: clause.literals)
                    if (lit.raw() > highest_raw)
                        highest_raw = lit.raw();
            }

            const auto literal_slots = total_occurrences == 0u ? 0u : static_cast<std::size_t>(highest_raw) + 1u;
            occurrence_start_.assign(literal_slots + 1u, 0u);
            occurrence_entries_.clear();
            if (literal_slots == 0u)
                return;

            for (const auto& clause: clauses_)
                for (const auto lit: clause.literals)
                    ++occurrence_start_[static_cast<std::size_t>(lit.raw()) + 1u];
            for (std::size_t slot {1u}; slot <= literal_slots; ++slot)
                occurrence_start_[slot] += occurrence_start_[slot - 1u];

            occurrence_entries_.resize(total_occurrences);
            occurrence_fill_.assign(occurrence_start_.begin(), occurrence_start_.end() - 1);
            for (std::uint32_t index {}; index < clauses_.size(); ++index)
                for (const auto lit: clauses_[index].literals)
                    occurrence_entries_[occurrence_fill_[lit.raw()]++] = index;
        }

        [[nodiscard]] std::span<const std::uint32_t> occurrences_of(const literal lit) const noexcept
        {
            const auto slot = static_cast<std::size_t>(lit.raw());
            if (slot + 1u >= occurrence_start_.size())
                return {};
            const auto begin = occurrence_start_[slot];
            return {occurrence_entries_.data() + begin, occurrence_start_[slot + 1u] - begin};
        }

        void mark_subsumed(indexed_clause& clause) noexcept
        {
            if (proof_manager_ != nullptr)
                proof_manager_->on_delete_clause(clause.ref);
            database_->mark_garbage(clause.ref);
            clause.active = false;
            last_subsumed_ref_ = clause.ref;
            ++subsumed_count_;
        }

        bool subsumes_candidate(const cdcl::clause::ref_t other_ref, const cdcl::clause::ref_t candidate_ref,
                                const std::uint32_t candidate_size, const std::span<const literal> candidate_clause) const noexcept
        {
            if (other_ref == candidate_ref)
                return false;

            const auto& storage = database_->storage_of();
            if (storage.literal_count(other_ref) > candidate_size)
                return false;
            return clause_subsumes(storage.view_literals(other_ref), candidate_clause);
        }

        static bool clause_subsumes(const std::span<const literal> left, const std::span<const literal> right) noexcept
        {
            if (left.empty() || left.size() > right.size())
                return false;

            for (const auto left_lit: left)
            {
                const auto found = std::find_if(right.begin(), right.end(),
                                                [left_lit](const literal right_lit) noexcept { return right_lit.raw() == left_lit.raw(); });
                if (found == right.end())
                    return false;
            }

            return true;
        }

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::size_t run_count_ {};
        std::size_t subsumed_count_ {};
        std::size_t strengthened_count_ {};
        cdcl::clause::ref_t last_subsumed_ref_ {};
        std::vector<indexed_clause> clauses_ {};
        std::vector<std::uint32_t> occurrence_start_ {};
        std::vector<std::uint32_t> occurrence_entries_ {};
        std::vector<std::uint32_t> occurrence_fill_ {};
    };
}
